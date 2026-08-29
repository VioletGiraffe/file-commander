#include "ciconprovider.h"
#include "ciconproviderimpl.h"
#include "cfilesystemobject.h"

#include "assert/advanced_assert.h"
#include "hash/wheathash.hpp"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <utility>

// A flat cap with a wholesale flush: telling the entries worth keeping from the rest would cost more than
// re-reading the few that get asked for again. 32k entries is well under a megabyte.
static constexpr size_t maxCachedObjects = 32768;

#ifdef _WIN32

#include "system/win_utils.hpp"
#include "threading/thread_helpers.h"

#include <Windows.h>

enum { DeliverIconsTag };

// Deep enough to hold several screens of backlog, shallow enough that a query stuck on an unreachable path isn't
// holding up much. Anything dropped is asked for again the next time its row is painted.
static constexpr size_t maxPendingRequests = 512;

// Hashes the pixels, not the QImage: two icons that look alike must land on one cache entry. Row by row, because a
// scanline can carry padding that was never initialized.
[[nodiscard]] static uint64_t imageContentHash(const QImage& image) noexcept
{
	uint64_t hash = wheathash64v(static_cast<int>(image.format()));
	hash = wheathash64v(image.width(), hash);
	hash = wheathash64v(image.height(), hash);

	const auto bytesPerRow = static_cast<uint64_t>(image.width()) * static_cast<uint64_t>(image.depth() / 8);
	for (int y = 0, height = image.height(); y < height; ++y)
		hash = wheathash64(image.constScanLine(y), bytesPerRow, hash);

	return hash;
}

[[nodiscard]] static QIcon iconFromImage(const QImage& image)
{
	return image.isNull() ? QIcon{} : QIcon{ QPixmap::fromImage(image) };
}

#endif

CIconProvider::CIconProvider() : _provider{std::make_unique<CIconProviderImpl>()}
{
	settingsChanged();
	watchForAppearanceChanges();

#ifdef _WIN32
	_wakeEvent = ::CreateEventW(nullptr, FALSE /* auto-reset */, FALSE, nullptr);
	assert_r(_wakeEvent);
	if (_wakeEvent)
		_retrievalThread = std::thread{ [this] { iconRetrievalThreadFunc(); } };
#endif
}

CIconProvider::~CIconProvider()
{
	shutdown();

#ifdef _WIN32
	if (_wakeEvent)
		::CloseHandle(_wakeEvent);
#endif
}

QIcon CIconProvider::genericIconForExtension(const CFileSystemObject& object)
{
	if (!object.isValid())
		return {};

#ifdef _WIN32
	if (object.isDir())
	{
		if (!_genericFolderIcon)
		{
			QIcon icon = iconFromImage(_provider->genericIconImage({}, true));
			if (icon.isNull())
				return {};

			_genericFolderIcon = std::move(icon);
		}

		return *_genericFolderIcon;
	}

	QString extension = object.extension();
	if (const auto it = _genericIconByExtension.find(extension); it != _genericIconByExtension.end())
		return it->second;

	QIcon icon = iconFromImage(_provider->genericIconImage(extension, false));
	if (icon.isNull())
		return {};

	return _genericIconByExtension.emplace(std::move(extension), std::move(icon)).first->second;
#else
	// QFileIconProvider answers both questions the same way, so this shares the other cache rather than filling a
	// second one with the same icons.
	return preciseIconBlocking(object);
#endif
}

QIcon CIconProvider::preciseIconBlocking(const CFileSystemObject& object)
{
	if (!object.isValid())
		return {};

	const qulonglong objectHash = object.hash();
	const time_t modificationTime = object.modificationTime();
	if (QIcon cached = cachedPreciseIcon(objectHash, modificationTime); !cached.isNull())
		return cached;

	FetchedIcon fetched = fetchPreciseIcon(object);
	if (fetched.icon.isNull())
		return {};

	QIcon icon = fetched.icon;
	cachePreciseIcon(objectHash, modificationTime, std::move(fetched));
	return icon;
}

QIcon CIconProvider::bestAvailableIconFor(const CFileSystemObject& object)
{
	if (!object.isValid())
		return {};

#ifdef _WIN32
	const qulonglong objectHash = object.hash();
	if (QIcon cached = cachedPreciseIcon(objectHash, object.modificationTime()); !cached.isNull())
		return cached;

	requestPreciseIcon(object, objectHash);
	return genericIconForExtension(object);
#else
	return preciseIconBlocking(object);
#endif
}

void CIconProvider::addIconsReadyListener(IconsReadyListener* listener)
{
	_iconsReadyListeners.addSubscriber(listener);
}

void CIconProvider::removeIconsReadyListener(IconsReadyListener* listener)
{
	_iconsReadyListeners.removeSubscriber(listener);
}

void CIconProvider::uiThreadTimerTick()
{
	_uiThreadQueue.exec();
}

void CIconProvider::settingsChanged()
{
	_provider->setShowOverlayIcons(QSettings{}.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());
	invalidateCache();
}

void CIconProvider::invalidateCache()
{
	_cachedIconByObjectHash.clear();
	_iconByContentHash.clear();
	_genericIconByExtension.clear();
	_genericFolderIcon.reset();

#ifdef _WIN32
	++_requestGeneration;
	_requestedObjects.clear();

	std::lock_guard locker{ _queueMutex };
	_pendingRequests.clear();
	_retrievedIcons.clear();
#endif
}

void CIconProvider::watchForAppearanceChanges()
{
	// There are no screens to watch under a plain QCoreApplication, which the core tests run on.
	auto* application = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
	if (!application)
		return;

	// The shell sizes its icons from the system small-icon metric, which follows the display scale, so a scale
	// change leaves every cached pixmap the wrong size. Watching all the screens rather than working out which one
	// the panels are on costs a needless refetch when an unrelated monitor is rescaled.
	const auto dropOnScaleChange = [this](QScreen* screen) {
		connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { invalidateCache(); });
	};

	for (QScreen* screen : QGuiApplication::screens())
		dropOnScaleChange(screen);

	connect(application, &QGuiApplication::screenAdded, this, dropOnScaleChange);
	// The metric follows the primary screen, so it changes when a different screen becomes primary even though no
	// screen's own scale did.
	connect(application, &QGuiApplication::primaryScreenChanged, this, [this] { invalidateCache(); });
}

void CIconProvider::shutdown()
{
#ifdef _WIN32
	if (!_retrievalThread.joinable())
		return;

	_stopRequested = true;
	::SetEvent(_wakeEvent);
	// Waits out at most one query in flight, which an unreachable path can hold for as long as the shell's timeout.
	_retrievalThread.join();

	// After the join, so nothing can enqueue past it. CController::shutdown() destroys the panels right after
	// calling this, and a delivery that ran afterwards would repaint rows whose panel is gone.
	_uiThreadQueue.clear();
#endif
}

CIconProvider::FetchedIcon CIconProvider::fetchPreciseIcon(const CFileSystemObject& object) const
{
#ifdef _WIN32
	const QImage image = _provider->preciseIconImage(object.fullAbsolutePath());
	if (image.isNull())
		return {};

	return { iconFromImage(image), imageContentHash(image) };
#else
	// Nothing to hash the contents of: QFileIconProvider returns an icon of several resolutions rather than one
	// bitmap, and flattening it to hash it would give up the per-DPI choice among them. Icons that look alike are
	// therefore stored separately here, and only the entry cap bounds that.
	QIcon icon = _provider->iconFor(object);
	if (icon.isNull())
		return {};

	const auto contentHash = static_cast<uint64_t>(icon.cacheKey());
	return { std::move(icon), contentHash };
#endif
}

QIcon CIconProvider::cachedPreciseIcon(const qulonglong objectHash, const time_t modificationTime) const
{
	const auto cached = _cachedIconByObjectHash.find(objectHash);
	if (cached == _cachedIconByObjectHash.end() || cached->second.modificationTime != modificationTime)
		return {};

	const auto icon = _iconByContentHash.find(cached->second.contentHash);
	assert_and_return_r(icon != _iconByContentHash.end(), {}); // The two are only ever filled and cleared together
	return icon->second;
}

void CIconProvider::cachePreciseIcon(const qulonglong objectHash, const time_t modificationTime, FetchedIcon&& fetched)
{
	if (_cachedIconByObjectHash.size() >= maxCachedObjects) [[unlikely]]
	{
		// Both, always: a stored icon is only reachable through the index that names its content hash.
		_cachedIconByObjectHash.clear();
		_iconByContentHash.clear();
	}

	_cachedIconByObjectHash.insert_or_assign(objectHash, CachedIcon{ fetched.contentHash, modificationTime });
	_iconByContentHash.try_emplace(fetched.contentHash, std::move(fetched.icon));
}

#ifdef _WIN32

void CIconProvider::requestPreciseIcon(const CFileSystemObject& object, const qulonglong objectHash)
{
	if (!_requestedObjects.insert(objectHash).second)
		return; // Already queued or in flight; repainting the row must not queue it again

	{
		std::lock_guard locker{ _queueMutex };
		_pendingRequests.push_back(IconRequest{ object.fullAbsolutePath(), objectHash, object.modificationTime(), _requestGeneration });
		if (_pendingRequests.size() > maxPendingRequests)
		{
			// The oldest request is the one a scroll has most likely carried off screen already.
			_requestedObjects.erase(_pendingRequests.front().objectHash);
			_pendingRequests.pop_front();
		}
	}

	::SetEvent(_wakeEvent);
}

std::optional<CIconProvider::IconRequest> CIconProvider::takeNextRequest()
{
	std::lock_guard locker{ _queueMutex };
	if (_pendingRequests.empty())
		return {};

	// Newest first: those are the rows on screen now, and a fast scroll's leftovers age out at the other end
	// instead of being served.
	IconRequest request = std::move(_pendingRequests.back());
	_pendingRequests.pop_back();
	return request;
}

void CIconProvider::waitForWork()
{
	DWORD signaledIndex = 0;
	// Not a condition variable: a COM call marshalled into this apartment while the thread idles has to be
	// serviced, and only a COM-aware wait does that. Flags 0 - dispatch what COM requires, nothing more.
	const HRESULT waitResult = ::CoWaitForMultipleHandles(0, INFINITE, 1, &_wakeEvent, &signaledIndex);
	if (FAILED(waitResult)) [[unlikely]]
	{
		// Retrying a wait that cannot succeed would spin a core. The thread gives up instead, and every icon comes
		// from genericIconForExtension from here on.
		assert_unconditional_r("CoWaitForMultipleHandles failed, stopping background icon retrieval");
		_stopRequested = true;
	}
}

void CIconProvider::iconRetrievalThreadFunc()
{
	setThreadName("Icon retrieval thread");
	// One apartment for the thread's lifetime: shell icon handlers are COM objects, and entering the apartment per
	// query would unload and reload their DLLs between icons.
	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	while (!_stopRequested)
	{
		const std::optional<IconRequest> request = takeNextRequest();
		if (!request)
		{
			waitForWork();
			continue;
		}

		// Contained as the pool and the execution queue contain theirs: allocating an image can throw, and an
		// escape from a thread function terminates the process over one icon. The object stays in
		// _requestedObjects, so it keeps its generic icon until the next settings change.
		try
		{
			QImage image = _provider->preciseIconImage(request->path);
			const uint64_t contentHash = image.isNull() ? 0 : imageContentHash(image);

			{
				std::lock_guard locker{ _queueMutex };
				_retrievedIcons.push_back(RetrievedIcon{ std::move(image), request->objectHash, request->modificationTime, contentHash, request->generation });
			}
		}
		catch (const std::exception& e)
		{
			assert_unconditional_r(std::string{ "Exception retrieving an icon: " } + e.what());
			continue;
		}

		// Tagged: a burst of retrievals between two UI ticks collapses into a single delivery.
		_uiThreadQueue.enqueue([this] { deliverRetrievedIcons(); }, DeliverIconsTag);
	}
}

void CIconProvider::deliverRetrievedIcons()
{
	std::vector<RetrievedIcon> retrieved;
	{
		std::lock_guard locker{ _queueMutex };
		retrieved.swap(_retrievedIcons);
	}

	std::vector<qulonglong> updatedObjects;
	updatedObjects.reserve(retrieved.size());

	for (RetrievedIcon& retrievedIcon : retrieved)
	{
		// Cleared only here, so an object whose retrieval failed is asked for again the next time its row is painted.
		_requestedObjects.erase(retrievedIcon.objectHash);
		if (retrievedIcon.image.isNull() || retrievedIcon.generation != _requestGeneration)
			continue;

		// Stored against the modification time the request carried, not the object's current one: if the object
		// changed in the meantime, the next lookup finds the mismatch and asks again.
		cachePreciseIcon(retrievedIcon.objectHash, retrievedIcon.modificationTime, FetchedIcon{ iconFromImage(retrievedIcon.image), retrievedIcon.contentHash });
		updatedObjects.push_back(retrievedIcon.objectHash);
	}

	if (!updatedObjects.empty())
		_iconsReadyListeners.invokeCallback(&IconsReadyListener::onPreciseIconsAvailable, updatedObjects);
}

#endif

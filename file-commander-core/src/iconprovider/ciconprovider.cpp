#include "ciconprovider.h"
#include "ciconproviderimpl.h"
#include "cfilesystemobject.h"

#include "assert/advanced_assert.h"
#include "hash/wheathash.hpp"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
#include <QPixmap>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <utility>

// A flat cap with a wholesale flush: telling the entries worth keeping from the rest would cost more than
// re-reading the few that get asked for again. 32k entries is well under a megabyte.
static constexpr size_t maxCachedObjects = 32768;

#ifdef _WIN32

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
}

CIconProvider::~CIconProvider() = default;

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

	const uint64_t objectHash = object.hash();
	if (QIcon cached = cachedPreciseIcon(objectHash, object); !cached.isNull())
		return cached;

	FetchedIcon fetched = fetchPreciseIcon(object);
	if (fetched.icon.isNull())
		return {};

	QIcon icon = fetched.icon;
	cachePreciseIcon(objectHash, object, std::move(fetched));
	return icon;
}

void CIconProvider::settingsChanged()
{
	_provider->setShowOverlayIcons(QSettings{}.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());

	_cachedIconByObjectHash.clear();
	_iconByContentHash.clear();
	_genericIconByExtension.clear();
	_genericFolderIcon.reset();
}

CIconProvider::FetchedIcon CIconProvider::fetchPreciseIcon(const CFileSystemObject& object) const
{
#ifdef _WIN32
	const QImage image = _provider->preciseIconImage(object);
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

QIcon CIconProvider::cachedPreciseIcon(const uint64_t objectHash, const CFileSystemObject& object) const
{
	const auto cached = _cachedIconByObjectHash.find(objectHash);
	if (cached == _cachedIconByObjectHash.end() || cached->second.modificationTime != object.modificationTime())
		return {};

	const auto icon = _iconByContentHash.find(cached->second.contentHash);
	assert_and_return_r(icon != _iconByContentHash.end(), {}); // The two are only ever filled and cleared together
	return icon->second;
}

void CIconProvider::cachePreciseIcon(const uint64_t objectHash, const CFileSystemObject& object, FetchedIcon&& fetched)
{
	if (_cachedIconByObjectHash.size() >= maxCachedObjects) [[unlikely]]
	{
		// Both, always: a stored icon is only reachable through the index that names its content hash.
		_cachedIconByObjectHash.clear();
		_iconByContentHash.clear();
	}

	_cachedIconByObjectHash.insert_or_assign(objectHash, CachedIcon{ fetched.contentHash, object.modificationTime() });
	_iconByContentHash.try_emplace(fetched.contentHash, std::move(fetched.icon));
}

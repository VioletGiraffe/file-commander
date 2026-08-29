#pragma once

#include "detail/hashmap_helpers.h"

#include "compiler/compiler_warnings_control.h"
#include "threading/cexecutionqueue.h"
#include "utility/callback_caller.hpp"

#include <3rdparty/ankerl/unordered_dense.h>

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdint.h>
#include <thread>
#include <time.h>
#include <vector>

class CFileSystemObject;
class CIconProviderImpl;

struct IconsReadyListener {
	virtual ~IconsReadyListener() = default;

	// Objects whose precise icon has just entered the cache. Every listener hears about all of them, including the
	// ones it doesn't display.
	virtual void onPreciseIconsAvailable(const std::vector<qulonglong>& objectHashes) = 0;
};

// Answers icon queries for filesystem objects and caches the answers. Owned by CController; every public method
// belongs to the UI thread. A query that fails is never cached, so a null return always means "no icon", never
// "no icon yet". Only Windows retrieves in the background; the QFileIconProvider path elsewhere stays on the UI thread.
class CIconProvider final : public QObject
{
public:
	CIconProvider();
	~CIconProvider();

	CIconProvider(const CIconProvider&) = delete;
	CIconProvider& operator=(const CIconProvider&) = delete;

	// The icon of the object's type. Never accesses the disk.
	[[nodiscard]] QIcon genericIconForExtension(const CFileSystemObject& object);
	// The object's own icon, which .exe, .ico and .lnk derive from their contents. Accesses the disk on a cache
	// miss, so only for callers that ask about a handful of objects at a time.
	[[nodiscard]] QIcon preciseIconBlocking(const CFileSystemObject& object);
	// The object's own icon if it is already cached, otherwise its type's icon and a background request for the
	// precise one; listeners hear when that arrives. Never accesses the disk, so this is the one for the file list.
	[[nodiscard]] QIcon bestAvailableIconFor(const CFileSystemObject& object);

	void addIconsReadyListener(IconsReadyListener* listener);
	void removeIconsReadyListener(IconsReadyListener* listener);

	// Moves retrieved icons into the cache and notifies the listeners. Driven by CController's UI thread tick.
	void uiThreadTimerTick();

	// Re-reads the overlay setting and drops every cached icon, which the setting changes. Display-scale changes
	// are noticed without being told - see watchForAppearanceChanges.
	void settingsChanged();

	// Stops the retrieval thread; the destructor also calls it. Queries keep working afterwards, they just stop
	// being answered in the background.
	void shutdown();

private:
	struct CachedIcon {
		uint64_t contentHash;
		time_t modificationTime; // The object's, as of retrieval. A newer one means the icon may have changed with it.
	};

	struct FetchedIcon {
		QIcon icon;
		uint64_t contentHash = 0;
	};

	// Drops every cached icon and retires what is in flight. For anything that changes what the shell would answer.
	void invalidateCache();
	void watchForAppearanceChanges();

	[[nodiscard]] FetchedIcon fetchPreciseIcon(const CFileSystemObject& object) const;
	// Null unless the object has an entry that modificationTime still matches.
	[[nodiscard]] QIcon cachedPreciseIcon(qulonglong objectHash, time_t modificationTime) const;
	void cachePreciseIcon(qulonglong objectHash, time_t modificationTime, FetchedIcon&& fetched);

#ifdef _WIN32
	struct IconRequest {
		QString path;
		qulonglong objectHash = 0;
		time_t modificationTime = 0;
		uint64_t generation = 0;
	};

	struct RetrievedIcon {
		QImage image;
		qulonglong objectHash = 0;
		time_t modificationTime = 0;
		uint64_t contentHash = 0;
		uint64_t generation = 0;
	};

	void requestPreciseIcon(const CFileSystemObject& object, qulonglong objectHash);
	void iconRetrievalThreadFunc();
	[[nodiscard]] std::optional<IconRequest> takeNextRequest();
	// Blocks until there is work or shutdown, servicing the apartment meanwhile.
	void waitForWork();
	void deliverRetrievedIcons();
#endif

private:
	// Keyed on CFileSystemObject::hash(), the hash of the full path, so an entry is only ever reused for the path it
	// was made for. The indirection through the content hash lets a folder of same-type files share one stored icon.
	ankerl::unordered_dense::map<qulonglong, CachedIcon, IdentityHash> _cachedIconByObjectHash;
	ankerl::unordered_dense::segmented_map<uint64_t, QIcon, IdentityHash> _iconByContentHash;

	ankerl::unordered_dense::segmented_map<QString, QIcon, QStringHash> _genericIconByExtension;
	std::optional<QIcon> _genericFolderIcon;

	// Never fires off Windows: there, bestAvailableIconFor has the precise icon by the time it returns.
	CallbackCaller<IconsReadyListener> _iconsReadyListeners;
	CExecutionQueue _uiThreadQueue;

#ifdef _WIN32
	// Both containers below are shared with the retrieval thread.
	std::mutex _queueMutex;
	std::deque<IconRequest> _pendingRequests;
	std::vector<RetrievedIcon> _retrievedIcons;

	// UI thread only. An object stays here from the moment it is queued until its result is delivered, so that
	// repainting a row that is still waiting doesn't queue it a second time.
	ankerl::unordered_dense::set<qulonglong, IdentityHash> _requestedObjects;
	// UI thread only; stamped into every request. Bumped whenever the caches are dropped, retiring whatever was
	// already in flight under the conditions that no longer hold.
	uint64_t _requestGeneration = 0;

	using HANDLE = void*;
	HANDLE _wakeEvent = nullptr;
	std::thread _retrievalThread;
	std::atomic<bool> _stopRequested{ false };
#endif

	std::unique_ptr<CIconProviderImpl> _provider;
};

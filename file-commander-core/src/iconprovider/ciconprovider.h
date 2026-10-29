#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"
#include "qtcore_helpers/qstring_hash.hpp"
#include "threading/cexecutionqueue.h"
#include "utility/callback_caller.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QIcon>
#include <QImage>
#include <QObject>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdint.h>
#include <thread>
#include <time.h>
#include <vector>

class CFileSystemObject;
class CIconProviderImpl;

struct IconsChangedListener {
	virtual ~IconsChangedListener() = default;

	// Objects whose icon is no longer what the last query returned: repaint their rows. Every listener hears about
	// all of them, including the ones it doesn't display.
	virtual void onIconsChanged(const std::vector<qulonglong>& objectHashes) = 0;
	// Every icon handed out so far is stale, so everything on screen has to be repainted.
	virtual void onAllIconsChanged() = 0;
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

	// The icon of the object's type: its extension's, or the folder icon for a folder. Never accesses the disk.
	// macOS: returns preciseIconBlocking(object) instead: Qt offers no icon by type there.
	[[nodiscard]] QIcon genericIconFor(const CFileSystemObject& object);
	// The object's own icon, which .exe, .ico and .lnk derive from their contents. Accesses the disk on a cache
	// miss, so only for callers that ask about a handful of objects at a time.
	[[nodiscard]] QIcon preciseIconBlocking(const CFileSystemObject& object);
	// The icon for the file list. Windows: the object's own icon if it is already cached, otherwise its type's icon and
	// a background request for the precise one; listeners hear when that arrives. Never accesses the disk.
	// Elsewhere: returns preciseIconBlocking(object).
	[[nodiscard]] QIcon bestAvailableIconFor(const CFileSystemObject& object);

	void addIconsChangedListener(IconsChangedListener* listener);
	void removeIconsChangedListener(IconsChangedListener* listener);

	// Moves retrieved icons into the cache and notifies the listeners. Driven by CController's UI thread tick.
	void uiThreadTimerTick();

	// Re-reads the overlay setting and drops every cached icon, which the setting changes. Display-scale changes
	// are noticed without being told - see watchForAppearanceChanges.
	void settingsChanged();

	// Drops every cached icon, retires what is in flight, and has the listeners repaint. For anything that changes
	// what the shell would answer and that this class cannot observe itself, such as the system theme.
	void invalidateCache();

	// Marks the paths' cached icons stale. A stale icon is still returned, and a fresh one is requested; listeners
	// repaint the rows. For an item whose icon changed without its modification time, such as an overlay.
	// Windows: case and a trailing separator are ignored, so a path from the shell matches the object's.
	void markIconsStale(std::span<const QString> fullAbsolutePaths);
	void markIconStale(const QString& fullAbsolutePath) { markIconsStale({ &fullAbsolutePath, 1 }); }

	// Stops the retrieval thread; the destructor also calls it. Queries keep working afterwards, they just stop
	// being answered in the background.
	void shutdown();

private:
	struct CachedIcon {
		uint64_t contentHash;
		qulonglong objectHash; // What onIconsChanged reports
		time_t modificationTime; // The object's, as of retrieval. A newer one means the icon may have changed with it.
		bool stale = false; // Still served; the next query also requests a fresh one
	};

	struct FetchedIcon {
		QIcon icon;
		uint64_t contentHash = 0;
	};

	struct CachedLookup {
		QIcon icon; // Null on a miss
		bool stale = false;
	};

	void watchForAppearanceChanges();

#ifndef __APPLE__
	[[nodiscard]] QIcon fetchGenericIcon(const QString& extension, bool isDir) const;
#endif
	[[nodiscard]] FetchedIcon fetchPreciseIcon(const QString& fullAbsolutePath) const;
	// A miss unless the key has an entry that modificationTime still matches.
	[[nodiscard]] CachedLookup cachedPreciseIcon(uint64_t cacheKey, time_t modificationTime) const;
	void cachePreciseIcon(uint64_t cacheKey, qulonglong objectHash, time_t modificationTime, FetchedIcon&& fetched);

#ifdef _WIN32
	struct IconRequest {
		QString path;
		uint64_t cacheKey = 0;
		qulonglong objectHash = 0;
		time_t modificationTime = 0;
		uint64_t generation = 0;
	};

	struct RetrievedIcon {
		QImage image;
		uint64_t cacheKey = 0;
		qulonglong objectHash = 0;
		time_t modificationTime = 0;
		uint64_t contentHash = 0;
		uint64_t generation = 0;
	};

	void requestPreciseIcon(const QString& fullAbsolutePath, uint64_t cacheKey, qulonglong objectHash, time_t modificationTime);
	void iconRetrievalThreadFunc();
	[[nodiscard]] std::optional<IconRequest> takeNextRequest();
	// Blocks until there is work or shutdown, servicing the apartment meanwhile.
	void waitForWork();
	void deliverRetrievedIcons();
#endif

private:
	// Keyed on iconCacheKey() of the full path, so an entry is only ever reused for the path it was made for.
	// The indirection through the content hash lets a folder of same-type files share one stored icon.
	ankerl::unordered_dense::map<uint64_t, CachedIcon, identity_hash> _cachedIconByPathKey;
	ankerl::unordered_dense::segmented_map<uint64_t, QIcon, identity_hash> _iconByContentHash;

	ankerl::unordered_dense::segmented_map<QString, QIcon, QStringHash, std::equal_to<>> _genericIconByExtension;
	std::optional<QIcon> _genericFolderIcon;

	CallbackCaller<IconsChangedListener> _iconsChangedListeners;
	CExecutionQueue _uiThreadQueue;

#ifdef _WIN32
	// Both containers below are shared with the retrieval thread.
	std::mutex _queueMutex;
	std::deque<IconRequest> _pendingRequests;
	std::vector<RetrievedIcon> _retrievedIcons;

	// UI thread only. A key stays here from the moment it is queued until its result is delivered, so that
	// repainting a row that is still waiting doesn't queue it a second time.
	ankerl::unordered_dense::set<uint64_t, identity_hash> _requestedKeys;
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

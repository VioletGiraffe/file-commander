#pragma once

#include "fileglyph.h"


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
#include <variant>
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

// Answers icon queries for filesystem objects and caches the answers. Every public method belongs to the UI thread.
// A query that fails is never cached, so a null return always means "no icon", never "no icon yet".
// Only Windows retrieves in the background; the QFileIconProvider path elsewhere stays on the UI thread.
class CIconProvider final : public QObject
{
public:
	CIconProvider();
	~CIconProvider();

	CIconProvider(const CIconProvider&) = delete;
	CIconProvider& operator=(const CIconProvider&) = delete;

	// An icon from the OS, or one of the app's glyphs
	using FileIcon = std::variant<QIcon, FileGlyph>;

	// The icon of the object's type: its extension's from the OS, the file glyph for a type no program claims, the folder
	// glyph for a folder. Never accesses the disk.
	// macOS: a glyph for every object: Qt offers no icon by type there.
	[[nodiscard]] FileIcon genericIconFor(const CFileSystemObject& object);
	// The object's own icon from the OS, which .exe, .ico and .lnk derive from their contents. Accesses the disk on a
	// cache miss, so only for callers that ask about a handful of objects at a time.
	[[nodiscard]] QIcon preciseIconBlocking(const CFileSystemObject& object);
	// The icon for the file list. The OS's default folder icon and its default file icon give way to the glyphs.
	// Windows: the object's own icon if it is cached, otherwise genericIconFor(object). A missing or stale icon is
	// requested in the background; listeners hear when it arrives. Never accesses the disk.
	// macOS: the object's own icon. Linux: a folder's own icon, a file's type icon. Both access the disk on a cache miss.
	[[nodiscard]] FileIcon bestAvailableIconFor(const CFileSystemObject& object);
	// The QIcon to draw; a glyph's is in the palette's text colour
	[[nodiscard]] QIcon qIcon(const FileIcon& icon) const;

	void addIconsChangedListener(IconsChangedListener* listener);
	void removeIconsChangedListener(IconsChangedListener* listener);

	// Moves retrieved icons into the cache and notifies the listeners. Driven by CMainWindow's UI thread timer.
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
		uint64_t contentHash = 0;
		bool stale = false;
	};

	void watchForAppearanceChanges();

#ifndef __APPLE__
	// The OS's icon for files with this extension, or the file glyph for a type no program claims. Empty if the OS gave none.
	[[nodiscard]] std::optional<FileIcon> fetchTypeIcon(const QString& extension);
#endif
	// The glyph for the object if `contentHash` is that of the OS's default folder or file icon, otherwise `icon`
	[[nodiscard]] FileIcon glyphInPlaceOfDefault(const QIcon& icon, uint64_t contentHash, bool isDir);
	// The content hash of the OS's default folder or file icon. Empty, and not kept, if the OS gave none.
	// Windows: the default file icon is the shell's icon for a type no program claims.
	[[nodiscard]] std::optional<uint64_t> defaultIconHash(bool isDir);
	// preciseIconBlocking(), with the icon's content hash
	[[nodiscard]] CachedLookup preciseLookupBlocking(const CFileSystemObject& object);
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

	QIcon _folderGlyph, _fileGlyph;
#ifndef __APPLE__
	ankerl::unordered_dense::segmented_map<QString, FileIcon, QStringHash, std::equal_to<>> _typeIconByExtension;
#endif
	std::optional<uint64_t> _defaultFolderIconHash, _defaultFileIconHash; // See defaultIconHash()

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

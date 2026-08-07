#pragma once

#include "detail/file_list_hashmap.h"
#include "cfilesystemobject.h"
#include "detail/hashmap_helpers.h"
#include "historylist/chistorylist.h"
#include "threading/cthreadpool.h"
#include "threading/cexecutionqueue.h"
#include "fileoperationresultcode.h"
#include "utility/callback_caller.hpp"

#ifdef _WIN32
#include "filesystemwatcher/cfilesystemwatcherwindows.h"
using FileSystemWatcher = CFileSystemWatcherWindows;
#else
#include "filesystemwatcher/cfilesystemwatchertimerbased.h"
using FileSystemWatcher = CFileSystemWatcherTimerBased;
#endif

#include <3rdparty/ankerl/unordered_dense.h>

#include <atomic>
#include <stdint.h>
#include <mutex>
#include <utility>
#include <vector>

enum class Panel
{
	LeftPanel,
	RightPanel,
	UnknownPanel
};

enum FileListRefreshCause
{
	refreshCauseForwardNavigation,
	refreshCauseCdUp,
	refreshCauseOther
};

// Callbacks name the tab they come from: a listener is attached to every tab of a side, so a listener that only
// deals with the one on screen (as opposed to, say, persisting all of them) must compare tabId against
// CController::activeTabId() rather than assume the notification is about the tab it's showing. Applies to
// CurrentItemChangedListener below as well.
struct PanelContentsChangedListener
{
	virtual ~PanelContentsChangedListener() = default;

	virtual void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) = 0;
	// The tab has moved to a view whose listing isn't ready yet, so the contents it reports are empty until the
	// onPanelContentsChanged that follows. Handle display only: anything derived from the contents (current item,
	// selection, persisted state, data published to plugins) must wait for that notification.
	virtual void onPanelContentsInvalidated(Panel p, qulonglong tabId) = 0;
};

struct CurrentItemChangedListener {
	virtual ~CurrentItemChangedListener() = default;

	// Delivered asynchronously, so folder names the directory the item was recorded for: the tab may have
	// navigated elsewhere in the meantime, where that item isn't the current one.
	virtual void onCurrentItemChanged(Panel p, qulonglong tabId, const QString& folder, qulonglong currentItemHash) = 0;
};

// Fires only when the panel's current directory actually changes (not on every refresh, unlike
// PanelContentsChangedListener). Distinct from the per-tab back/forward history: a tab's owner uses this
// to maintain a navigation-independent log of visited locations.
struct CurrentPathChangedListener {
	virtual ~CurrentPathChangedListener() = default;

	virtual void onCurrentPathChanged(Panel p, const QString& newPath) = 0;
};

class CPanel final
{
public:
	enum CurrentDisplayMode {NormalMode, AllObjectsMode};

	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);
	void addCurrentItemChangedListener(CurrentItemChangedListener * listener);
	void addCurrentPathChangedListener(CurrentPathChangedListener * listener);

	explicit CPanel(Panel position, CThreadPool& workerThreadPool, qulonglong id);
	~CPanel();

	[[nodiscard]] qulonglong id() const noexcept; // Stable per-tab identifier; never changes for this CPanel's lifetime

	// Seeds the navigation history when restoring a tab from saved settings. The controller owns persistence now,
	// so CPanel no longer reads or writes QSettings itself.
	void restoreHistory(const std::vector<QString>& history);
	// Activates/deactivates this tab. An inactive tab releases its filesystem watch handle; activating re-arms the watch and
	// refreshes the file list (the folder's changes weren't being watched while the tab was inactive).
	void setActive(bool active);
	// Sets the current directory
	FileOperationResultCode setPath(const QString& path, FileListRefreshCause operation);
	// Navigates up the directory tree
	void navigateUp();
	// Go to the previous location from history
	bool navigateBack();
	// Go to the next location from history, if any
	bool navigateForward();
	[[nodiscard]] const CHistoryList<QString>& history() const;
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow();
	// Switches to the appropriate directory and sets the cursor to the specified item
	bool goToItem(const CFileSystemObject& item);

	// Info on the dir this panel is currently set to
	[[nodiscard]] CFileSystemObject currentDirObject() const;
	[[nodiscard]] QString currentDirPathNative() const;
	[[nodiscard]] QString currentDirPathPosix() const;
	[[nodiscard]] QString currentDirName() const;

	void setCurrentItemHashForFolder(const QString& dir, qulonglong currentItemHash, bool notifyUi = true);
	// Returns hash of an item that was the last selected in the specified dir
	[[nodiscard]] qulonglong currentItemHashForFolder(const QString& dir) const;

	// Enumerates objects in the current directory
	void refreshFileList(FileListRefreshCause operation);
	// Returns the current list of objects on this panel
	[[nodiscard]] FileListHashMap list() const;

	[[nodiscard]] bool itemHashExists(qulonglong hash) const;
	[[nodiscard]] CFileSystemObject itemByHash(qulonglong hash) const;
	[[nodiscard]] QString itemPathByHash(qulonglong hash) const;
	[[nodiscard]] std::vector<QString> itemPathsByHashes(const std::vector<qulonglong>& hashes) const;

	[[nodiscard]] std::vector<qulonglong> itemHashes() const;

	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(qulonglong dirHash);

	void uiThreadTimerTick();

private:
	struct FileListUpdateRequest
	{
		uint64_t generation;
		QString path;
		CurrentDisplayMode displayMode;
	};

	[[nodiscard]] bool pathIsAccessible(const QString& path) const;
	[[nodiscard]] FileListUpdateRequest beginFileListUpdateLocked(CurrentDisplayMode displayMode);
	[[nodiscard]] bool fileListUpdateIsCurrentLocked(const FileListUpdateRequest& request) const;
	[[nodiscard]] bool fileListBelongsToCurrentViewLocked() const;

	void enqueueFileListUpdate(FileListUpdateRequest request, FileListRefreshCause operation);
	void publishFileListIfCurrent(const FileListUpdateRequest& request, FileListHashMap&& items, FileListRefreshCause operation);
	void recoverFromInaccessiblePathIfCurrent(const FileListUpdateRequest& request);
	void sendContentsChangedNotification(FileListRefreshCause operation) const;
	void enqueueContentsChangedNotificationLocked(FileListRefreshCause operation, uint64_t generation) const;
	void enqueueContentsInvalidatedNotificationLocked(uint64_t generation) const;
	[[nodiscard]] bool fileListGenerationIsCurrent(uint64_t generation) const;

	void refreshIfWatcherDetectedChanges();

	template <typename Functor>
	void execOnUiThread(Functor&& f, int tag = -1) const noexcept
	{
		_uiThreadQueue.enqueue(std::forward<Functor>(f), tag);
	}

private:
	CFileSystemObject                          _currentDirObject;
	FileSystemWatcher                          _watcher;
	FileListHashMap                            _items;
	QString                                    _itemsSourcePath;
	CurrentDisplayMode                         _itemsSourceDisplayMode = NormalMode;
	uint64_t                                   _fileListGeneration = 0;
	CHistoryList<QString>                      _history;
	// UI thread only, hence unguarded. If ever something needs to access it from a different thread, add a mutex and lock it in the accessors.
	ankerl::unordered_dense::segmented_map<QString, qulonglong /*hash*/, QStringHash> _currentItemHashForFolder;
	CallbackCaller<PanelContentsChangedListener> _panelContentsChangedListeners;
	CallbackCaller<CurrentItemChangedListener> _currentItemChangedListeners;
	CallbackCaller<CurrentPathChangedListener> _currentPathChangedListeners;
	const Panel                                _panelPosition;
	const qulonglong                           _id; // Stable identity for this tab; assigned by CController. Distinct from _taskTag below.
	const uint64_t                             _taskTag; // Unique per panel; tags this panel's tasks in the shared pool so they can be retired when the panel is destroyed
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;

	CThreadPool&                               _workerThreadPool; // Shared pool owned by CController; this panel's tasks carry _taskTag
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;
	// Signals this panel's background scans to bail out. Currently set only during destruction, so retiring
	// the pool tasks doesn't block on a full recursive scan; usable by any future need to abort background work.
	std::atomic<bool>                          _abortBackgroundTasks{false};
};

#pragma once

#include "cfilesystemobject.h"
#include "fileoperationresultcode.h"
#include "detail/file_list_hashmap.h"
#include "filelist/filelistsorter.h"
#include "filelist/filenamefilter.h"
#include "filelist/foldercontentssummary.h"
#include "filesystemwatcher/cfilesystemwatcher.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"
#include "historylist/chistorylist.h"
#include "qtcore_helpers/qstring_hash.hpp"
#include "threading/cexecutionqueue.h"
#include "threading/cthreadpool.h"
#include "utility/callback_caller.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <functional>
#include <mutex>
#include <stddef.h>
#include <stdint.h>
#include <utility>
#include <vector>

class ListableFolderProbe;

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

	// Called from setPath with the panel's list mutex held
	virtual void onCurrentPathChanged(Panel p, const QString& newPath) = 0;
};

class CPanel final
{
public:
	enum CurrentDisplayMode {NormalMode, AllObjectsMode};

	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);
	void addCurrentItemChangedListener(CurrentItemChangedListener * listener);
	void addCurrentPathChangedListener(CurrentPathChangedListener * listener);

	explicit CPanel(Panel position, CThreadPool& listingThreadPool, CThreadPool& workerThreadPool, qulonglong id);
	~CPanel();

	[[nodiscard]] qulonglong id() const noexcept; // Stable per-tab identifier; never changes for this CPanel's lifetime

	// Seeds the navigation history when restoring a tab from saved settings.
	void restoreHistory(const std::vector<QString>& history);
	// Activates/deactivates this tab. An inactive tab holds no filesystem watch handle and doesn't list its folder at
	// all; activating arms the watch and lists the folder, which is also how it picks up what changed while inactive.
	void setActive(bool active);
	// Sets the current directory. An active tab lists it, landing on an accessible folder when it cannot be listed.
	// An inactive tab records an absolute path without reading the disk and returns Ok: setActive lists it.
	FileOperationResultCode setPath(const QString& path, FileListRefreshCause operation);
	// Navigates up the directory tree
	void navigateUp();
	// Go to the previous location from history whose folder can be listed
	bool navigateBack();
	// Go to the next location from history whose folder can be listed
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
	// Empty when the name filter hides it
	[[nodiscard]] CFileSystemObject currentItem() const;

	// Enumerates objects in the current directory
	void refreshFileList(FileListRefreshCause operation);
	// Invokes fn(folder, contents) under the list lock, or not at all if no committed listing belongs to the
	// current view. fn holds up this panel's refreshes, so it must be short.
	void readCommittedContents(const std::function<void(const QString& folder, const FileListHashMap& contents)>& fn) const;
	// Advances on every setPath() and showAllFilesFromCurrentFolderAndBelow(), even to the folder already in view; a refresh keeps it.
	// Changes only on the UI thread.
	[[nodiscard]] uint64_t navigationId() const;

	[[nodiscard]] bool itemHashExists(qulonglong hash) const;
	[[nodiscard]] CFileSystemObject itemByHash(qulonglong hash) const;

// View criteria: a front end shows the listing through them, and the selection and the command targets below obey them
	// filterText: a filter field's text (see splitNameFilterField()), matched against full names ignoring case; empty shows every item.
	// Deselects the items it hides.
	void setNameFilter(const QString& filterText);
	[[nodiscard]] QString nameFilter() const;
	void setSorting(FileListSortField field, Qt::SortOrder order);
	[[nodiscard]] FileListSortField sortField() const;
	[[nodiscard]] Qt::SortOrder sortOrder() const;
	// The items the name filter shows, in display order. Empty while the committed listing does not belong to the current view.
	[[nodiscard]] std::vector<CFileSystemObject> visibleItems() const;

// Selection: items of the committed listing, never [..] and never an item the name filter hides.
// A new listing keeps the selected items it still has at the same path, so a navigation to another folder clears it.
	// Ignores the hashes it cannot select
	void setItemsSelected(const std::vector<qulonglong>& hashes, bool selected);
	// Toggles every item it can select
	void invertSelection();
	// patternText: as setNameFilter's filterText. filesOnly leaves folders out; a bundle counts as a file.
	void setItemsSelectedByPattern(const QString& patternText, bool selected, bool filesOnly);
	// Adds the files whose extension equals the file itemHash's, ignoring case: without one, the files without one.
	// A bundle counts as a file. Nothing when itemHash is no file.
	void selectFilesWithExtensionOf(qulonglong itemHash);
	// Empty while the committed listing does not belong to the current view
	[[nodiscard]] std::vector<qulonglong> selectedItemHashes() const;
	// In display order
	[[nodiscard]] std::vector<CFileSystemObject> selectedItems() const;
	// selectedItems(), or with nothing selected the current item, unless that is [..] or hidden
	[[nodiscard]] std::vector<CFileSystemObject> selectedItemsOrCurrentItem() const;
	[[nodiscard]] FolderContentsSummary selectionSummary() const;
	// The selected items' paths, or with nothing selected the current item's, [..] included: shell-quoted, native, one per line.
	// Empty when there is no current item either.
	[[nodiscard]] QString selectedOrCurrentItemPathLines() const;

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

	struct FolderResolution
	{
		CFileSystemObject folder;
		bool listable = false; // False when no candidate is: folder is then the panel's own, or the requested one if it had none
		bool isRequestedPathOrAncestor = false;
	};

	// The folder setPath(path) moves to: the first listable one of path and its ancestors, currentDir, history from its cursor
	// back, and home. Checks the candidates on disk through probe: recovery runs it on a worker.
	[[nodiscard]] static FolderResolution resolveFolder(const QString& path, const CFileSystemObject& currentDir, const CHistoryList<QString>& history, ListableFolderProbe& probe);
	void moveToFolder(FolderResolution resolution, FileListRefreshCause operation);
	// Runs on the worker that found request's folder inaccessible
	void recoverFromInaccessibleFolder(const FileListUpdateRequest& request);

	[[nodiscard]] CFileSystemObject currentItemLocked() const;
	[[nodiscard]] bool isSelectableLocked(const CFileSystemObject& item) const;
	[[nodiscard]] std::vector<CFileSystemObject> selectedItemsInDisplayOrder(bool orCurrentItem) const;

	[[nodiscard]] FileListUpdateRequest beginFileListUpdateLocked(CurrentDisplayMode displayMode);
	[[nodiscard]] bool fileListUpdateIsCurrentLocked(const FileListUpdateRequest& request) const;
	[[nodiscard]] bool fileListBelongsToCurrentViewLocked() const;

	// step is -1 for back, +1 for forward. The history cursor stays where it was when no entry in that direction can be listed.
	bool navigateToNearestAccessibleHistoryEntry(ptrdiff_t step);

	void enqueueFileListUpdate(FileListUpdateRequest request, FileListRefreshCause operation);
	void publishFileListIfCurrent(const FileListUpdateRequest& request, FileListHashMap&& items, FileListRefreshCause operation);
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
	CFileSystemWatcher                         _watcher;
	FileListHashMap                            _items;
	// Folder sizes calculated on request, re-applied to every listing that still has the folder. Protected by _fileListAndCurrentDirMutex.
	ankerl::unordered_dense::map<qulonglong /*hash*/, uint64_t, identity_hash> _calculatedDirSizes;
	QString                                    _itemsSourcePath;
	CurrentDisplayMode                         _itemsSourceDisplayMode = NormalMode;
	uint64_t                                   _fileListGeneration = 0;
	uint64_t                                   _navigationId = 0;
	// Written only on the UI thread, under _fileListAndCurrentDirMutex: recovery copies it on a worker
	CHistoryList<QString>                      _history;
	// Protected by _fileListAndCurrentDirMutex so the plugin-facing current-item snapshot is coherent with the list.
	ankerl::unordered_dense::segmented_map<QString, qulonglong /*hash*/, QStringHash> _currentItemHashForFolder;
	// The selection and the view criteria are protected by _fileListAndCurrentDirMutex: plugins read the selection from
	// worker threads, and committing a listing on a worker prunes it
	ankerl::unordered_dense::set<qulonglong, identity_hash> _selectedItemHashes;
	QString                                    _nameFilterText;
	FileNameFilter                             _nameFilter;
	FileListSortField                          _sortField = FileListSortField::Extension; // A new tab's sorting
	Qt::SortOrder                              _sortOrder = Qt::AscendingOrder;
	CallbackCaller<PanelContentsChangedListener> _panelContentsChangedListeners;
	CallbackCaller<CurrentItemChangedListener> _currentItemChangedListeners;
	CallbackCaller<CurrentPathChangedListener> _currentPathChangedListeners;
	const Panel                                _panelPosition;
	const qulonglong                           _id; // Stable identity for this tab; assigned by CController. Distinct from _taskTag below.
	const uint64_t                             _taskTag; // Unique per panel; tags this panel's tasks in the shared pools so they can be retired when the panel is destroyed
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;
	bool                                       _active = false;

	// Both pools are owned by CController and shared by all panels; this panel's tasks in either carry _taskTag
	CThreadPool&                               _listingThreadPool; // File list updates only, so other work cannot delay a listing
	CThreadPool&                               _workerThreadPool; // Folder size calculations
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;
	// Signals this panel's background scans to bail out. Currently set only during destruction, so retiring
	// the pool tasks doesn't block on a full recursive scan; usable by any future need to abort background work.
	std::atomic<bool>                          _abortBackgroundTasks{false};
};

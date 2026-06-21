#pragma once

#include "detail/file_list_hashmap.h"
#include "cfilesystemobject.h"
#include "detail/hashmap_helpers.h"
#include "historylist/chistorylist.h"
#include "threading/cworkerthread.h"
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

#include <mutex>
#include <utility>
#include <vector>

enum class Panel
{
	LeftPanel,
	RightPanel,
	UnknownPanel
};

using TabId = uint64_t; // Stable identifier for a CPanel-as-tab, assigned once by CController when the tab is created

enum FileListRefreshCause
{
	refreshCauseForwardNavigation,
	refreshCauseCdUp,
	refreshCauseNewItemCreated,
	refreshCauseOther
};

struct PanelContentsChangedListener
{
	virtual ~PanelContentsChangedListener() = default;

	virtual void onPanelContentsChanged(Panel p, FileListRefreshCause operation) = 0;
};

struct CursorPositionListener {
	virtual ~CursorPositionListener() = default;

	virtual void setCursorToItem(const QString& folder, qulonglong currentItemHash) = 0;
};

class CPanel final
{
public:
	enum CurrentDisplayMode {NormalMode, AllObjectsMode};

	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);
	void addCurrentItemChangeListener(CursorPositionListener * listener);

	explicit CPanel(Panel position, CWorkerThreadPool& workerThreadPool, TabId id);
	~CPanel();

	[[nodiscard]] TabId id() const noexcept; // Stable per-tab identifier; never changes for this CPanel's lifetime

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

	void setCurrentItemForFolder(const QString& dir, qulonglong currentItemHash, bool notifyUi = true);
	// Returns hash of an item that was the last selected in the specified dir
	[[nodiscard]] qulonglong currentItemForFolder(const QString& dir) const;

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

	void sendContentsChangedNotification(FileListRefreshCause operation) const;

	void uiThreadTimerTick();

private:
	[[nodiscard]] bool pathIsAccessible(const QString& path) const;

	void processContentsChangedEvent();

	template <typename Functor>
	void execOnUiThread(Functor&& f, int tag = -1) const noexcept
	{
		_uiThreadQueue.enqueue(std::forward<Functor>(f), tag);
	}

private:
	CFileSystemObject                          _currentDirObject;
	FileSystemWatcher                          _watcher;
	FileListHashMap                            _items;
	CHistoryList<QString>                      _history;
	ankerl::unordered_dense::segmented_map<QString, qulonglong /*hash*/, QStringHash> _cursorPosForFolder;
	CallbackCaller<PanelContentsChangedListener> _panelContentsChangedListeners;
	CallbackCaller<CursorPositionListener>     _currentItemChangeListener;
	const Panel                                _panelPosition;
	const TabId                                _id; // Stable identity for this tab; assigned by CController. Distinct from _taskTag below.
	const uint64_t                             _taskTag; // Unique per panel; tags this panel's tasks in the shared pool so they can be retired when the panel is destroyed
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;

	CWorkerThreadPool&                         _workerThreadPool; // Shared pool owned by CController; this panel's tasks carry _taskTag
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;
};

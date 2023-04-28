#pragma once

#include "cfilesystemobject.h"
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

#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <utility>

enum Panel
{
	LeftPanel,
	RightPanel,
	UnknownPanel
};

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

	virtual void panelContentsChanged(Panel p, FileListRefreshCause operation) = 0;
	// progress > 100 means indefinite
	virtual void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress, const QString& currentDir) = 0;
};

struct FilesystemObjectsStatistics
{
	inline FilesystemObjectsStatistics(uint64_t files_ = 0, uint64_t folders_ = 0, uint64_t occupiedSpace_ = 0): files(files_), folders(folders_), occupiedSpace(occupiedSpace_) {}
	[[nodiscard]] inline bool empty() const {return files == 0 && folders == 0 && occupiedSpace == 0;}

	uint64_t files;
	uint64_t folders;
	uint64_t occupiedSpace;
};

struct CursorPositionListener {
	virtual ~CursorPositionListener() = default;

	virtual void setCursorToItem(const QString& folder, qulonglong currentItemHash) = 0;
};

class CPanel final : public QObject
{
public:
	enum CurrentDisplayMode {NormalMode, AllObjectsMode};

	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);
	void addCurrentItemChangeListener(CursorPositionListener * listener);

	explicit CPanel(Panel position);
	~CPanel() override;

	void restoreFromSettings();
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
	[[nodiscard]] std::map<qulonglong, CFileSystemObject> list() const;

	[[nodiscard]] bool itemHashExists(qulonglong hash) const;
	[[nodiscard]] CFileSystemObject itemByHash(qulonglong hash) const;

	// Calculates total size for the specified objects
	[[nodiscard]] FilesystemObjectsStatistics calculateStatistics(const std::vector<qulonglong> & hashes);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(qulonglong dirHash);

	void sendContentsChangedNotification(FileListRefreshCause operation) const;
	// progress > 100 means indefinite
	void sendItemDiscoveryProgressNotification(qulonglong itemHash, size_t progress, const QString& currentDir) const;

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
	std::map<qulonglong, CFileSystemObject>    _items;
	CHistoryList<QString>                      _history;
	std::map<QString, qulonglong /*hash*/>     _cursorPosForFolder;
	CallbackCaller<PanelContentsChangedListener> _panelContentsChangedListeners;
	CallbackCaller<CursorPositionListener>    _currentItemChangeListener;
	const Panel                                _panelPosition;
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;

	CWorkerThreadPool                          _workerThreadPool;
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;
};

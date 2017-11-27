#ifndef CPANEL_H
#define CPANEL_H

#include "cfilesystemobject.h"
#include "diskenumerator/cvolumeenumerator.h"
#include "historylist/chistorylist.h"
#include "threading/cworkerthread.h"
#include "threading/cexecutionqueue.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

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
	virtual void panelContentsChanged(Panel p, FileListRefreshCause operation) = 0;
	// progress > 100 means indefinite
	virtual void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress, const QString& currentDir) = 0;
};

class FilesystemObjectsStatistics
{
public:
	FilesystemObjectsStatistics(uint64_t files_ = 0, uint64_t folders_ = 0, uint64_t occupiedSpace_ = 0): files(files_), folders(folders_), occupiedSpace(occupiedSpace_) {}
	bool empty() const {return files == 0 && folders == 0 && occupiedSpace == 0;}

	uint64_t files;
	uint64_t folders;
	uint64_t occupiedSpace;
};

class CPanel : public QObject
{
public:
	enum CurrentDisplayMode {NormalMode, AllObjectsMode};

	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);

	explicit CPanel(Panel position);
	void restoreFromSettings();
	// Sets the current directory
	FileOperationResultCode setPath(const QString& path, FileListRefreshCause operation);
	// Navigates up the directory tree
	void navigateUp();
	// Go to the previous location from history
	bool navigateBack();
	// Go to the next location from history, if any
	bool navigateForward();
	const CHistoryList<QString>& history() const;
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow();
	// Switches to the appropriate directory and sets the cursor to the specified item
	bool goToItem(const CFileSystemObject& item);

	// Info on the dir this panel is currently set to
	CFileSystemObject currentDirObject() const;
	QString currentDirPathNative() const;
	QString currentDirPathPosix() const;
	QString currentDirName() const;

	void setCurrentItemForFolder(const QString& dir, qulonglong currentItemHash);
	// Returns hash of an item that was the last selected in the specified dir
	qulonglong currentItemForFolder(const QString& dir) const;

	// Enumerates objects in the current directory
	void refreshFileList(FileListRefreshCause operation);
	// Returns the current list of objects on this panel
	std::map<qulonglong, CFileSystemObject> list() const;

	bool itemHashExists(const qulonglong hash) const;
	CFileSystemObject itemByHash(qulonglong hash) const;

	// Calculates total size for the specified objects
	FilesystemObjectsStatistics calculateStatistics(const std::vector<qulonglong> & hashes);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(qulonglong dirHash);

	void sendContentsChangedNotification(FileListRefreshCause operation) const;
	// progress > 100 means indefinite
	void sendItemDiscoveryProgressNotification(qulonglong itemHash, size_t progress, const QString& currentDir) const;

	void volumesChanged(const std::deque<VolumeInfo>& volumes);

	// Settings have changed
	void settingsChanged();

	void uiThreadTimerTick();

private:
	const VolumeInfo& volumeInfoForObject(const CFileSystemObject& object) const;
	bool pathIsAccessible(const QString& path) const;

	void contentsChanged();
	void processContentsChangedEvent();

private:
	CFileSystemObject                          _currentDirObject;
	std::map<qulonglong, CFileSystemObject>    _items;
	CHistoryList<QString>                      _history;
	std::map<QString, qulonglong /*hash*/>     _cursorPosForFolder;
	std::shared_ptr<class CFileSystemWatcher>  _watcher; // Can't use uniqe_ptr because it doesn't play nicely with forward declaration
	std::vector<PanelContentsChangedListener*> _panelContentsChangedListeners;
	const Panel                                _panelPosition;
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;

	std::deque<VolumeInfo> _volumes;

	CWorkerThreadPool                          _workerThreadPool;
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;

	QTimer                                     _fileListRefreshTimer;
	std::atomic<bool>                          _bContentsChangedEventPending{false};
};

#endif // CPANEL_H

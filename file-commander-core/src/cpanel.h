#ifndef CPANEL_H
#define CPANEL_H

#include <vector>
#include <map>
#include <memory>

#include "QtCoreIncludes"

#include "cfilesystemobject.h"
#include "diskenumerator/cdiskenumerator.h"
#include "historylist/chistorylist.h"
#include "utils/threading/casynctask.h"
#include "utils/threading/cexecutionqueue.h"

#include <mutex>

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
	virtual void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress) = 0;
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

class QFileSystemWatcher;

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

	// Info on the dir this panel is currently set to
	QString currentDirPathNative() const;
	QString currentDirPathPosix() const;
	QString currentDirName() const;

	void setCurrentItemInFolder(const QString& dir, qulonglong currentItemHash);
	// Returns hash of an item that was the last selected in the specified dir
	qulonglong currentItemInFolder(const QString& dir) const;

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
	void sendItemDiscoveryProgressNotification(qulonglong itemHash, size_t progress) const;

	void disksChanged(const std::vector<CDiskEnumerator::DiskInfo>& disks);

	// Settings have changed
	void settingsChanged();

	void uiThreadTimerTick();

private:
	const QStorageInfo& storageInfoForObject(const CFileSystemObject& object) const;
	bool pathIsAccessible(const QString& path) const;

	void contentsChanged(QString path);

private:
	CFileSystemObject                          _currentDirObject;
	std::map<qulonglong, CFileSystemObject>    _items;
	CHistoryList<QString>                      _history;
	std::map<QString, qulonglong /*hash*/>     _cursorPosForFolder;
	std::shared_ptr<QFileSystemWatcher>        _watcher;
	std::vector<PanelContentsChangedListener*> _panelContentsChangedListeners;
	const Panel                                _panelPosition;
	CurrentDisplayMode                         _currentDisplayMode = NormalMode;

	std::vector<CDiskEnumerator::DiskInfo>     _disks;

	CAsyncTask<void>                           _refreshFileListTask;
	mutable CExecutionQueue                    _uiThreadQueue;
	mutable std::recursive_mutex               _fileListAndCurrentDirMutex;
};

#endif // CPANEL_H

#ifndef CPANEL_H
#define CPANEL_H

#include <vector>
#include <map>
#include <memory>

#include "QtCoreIncludes"

#include "cfilesystemobject.h"
#include "historylist/chistorylist.h"

enum Panel
{
	LeftPanel,
	RightPanel,
	UnknownPanel
};

enum NavigationOperation
{
	nopForward,
	nopCdUp,
	nopOther
};


struct PanelContentsChangedListener
{
	virtual void panelContentsChanged(Panel p, NavigationOperation operation) = 0;
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
	Q_OBJECT

public:
	void addPanelContentsChangedListener(PanelContentsChangedListener * listener);

	CPanel(Panel position);
	// Sets the current directory
	FileOperationResultCode setPath(const QString& path, NavigationOperation operation);
	// Navigates up the directory tree
	void navigateUp();
	// Go to the previous location from history
	bool navigateBack();
	// Go to the next location from history, if any
	bool navigateForward();
	const CHistoryList<QString>& history() const;

	// Info on the dir this panel is currently set to
	QString currentDirPath() const;
	QString currentDirName() const;

	void setCurrentItemInFolder(const QString& dir, qulonglong currentItemHash);
	// Returns hash of an item that was the last selected in the specified dir
	qulonglong currentItemInFolder(const QString& dir) const;

	// Enumerates objects in the current directory
	void refreshFileList(NavigationOperation operation);
	// Returns the current list of objects on this panel
	const std::vector<CFileSystemObject>& list () const;

	// Access to the corresponding item
	const CFileSystemObject& itemByIndex(size_t index) const;
	CFileSystemObject& itemByIndex(size_t index);

	bool itemHashExists(const qulonglong hash) const;
	const CFileSystemObject& itemByHash(qulonglong hash) const;
	CFileSystemObject& itemByHash(qulonglong hash);

	// Calculates total size for the specified objects
	FilesystemObjectsStatistics calculateStatistics(const std::vector<qulonglong> & hashes);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(qulonglong dirHash);

	void sendContentsChangedNotification(NavigationOperation operation) const;

	// Settings have changed
	void settingsChanged();

private slots:
	void contentsChanged(QString path);

private:
	QDir                                       _currentDir;
	std::vector<CFileSystemObject>             _list;
	CHistoryList<QString>                      _history;
	std::map<QString, qulonglong /*hash*/>     _cursorPosForFolder;
	std::map<qulonglong, size_t>               _indexByHash;
	std::shared_ptr<QFileSystemWatcher>        _watcher;
	std::vector<PanelContentsChangedListener*> _panelContentsChangedListeners;
	const Panel                                _panelPosition;
};

#endif // CPANEL_H

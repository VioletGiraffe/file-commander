#ifndef CCONTROLLER_H
#define CCONTROLLER_H

#include "QtCoreIncludes"

#include "fileoperationresultcode.h"
#include "cpanel.h"
#include "diskenumerator/cdiskenumerator.h"
#include "plugininterface/cpluginproxy.h"
#include "favoritelocationslist/cfavoritelocations.h"

class CController : private CDiskEnumerator::IDiskListObserver
{
public:
	// Disk list observer interface
	class IDiskListObserver
	{
	public:
		virtual ~IDiskListObserver() {}
		virtual void disksChanged(const std::vector<CDiskEnumerator::DiskInfo>& drives, Panel p) = 0;
	};

	CController();
	static CController& get();

	void setPanelContentsChangedListener(Panel p, PanelContentsChangedListener * listener);
	void setDisksChangedListener(IDiskListObserver * listener);

// Notifications from UI
	void uiThreadTimerTick();

	// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
	void refreshPanelContents(Panel p);
	// Creates a new tab for the specified panel, returns tab ID
	int tabCreated (Panel p);
	// Removes a tab for the specified panel and tab ID
	void tabRemoved(Panel panel, int tabId);
	// Indicates that an item was activated and appropriate action should be taken.  Returns error message, if any
	FileOperationResultCode itemActivated(qulonglong itemHash, Panel p);
	// A current disk has been switched
	bool switchToDisk(Panel p, int index);
	// Program settings have changed
	void settingsChanged();
	// Focus is set to a panel
	void activePanelChanged(Panel p);

// Operations
	// Navigates specified panel up the directory tree
	void navigateUp(Panel p);
	// Go to the previous location from history, if any
	void navigateBack(Panel p);
	// Go to the next location from history, if any
	void navigateForward(Panel p);
	// Sets the specified path, if possible. Otherwise reverts to the previously set path
	FileOperationResultCode setPath(Panel p, const QString& path, FileListRefreshCause operation);
	// Creates a folder with a specified name at the specified parent folder
	bool createFolder(const QString& parentFolder, const QString& name);
	// Creates a file with a specified name at the specified parent folder
	bool createFile(const QString& parentFolder, const QString& name);
	// Opens a terminal window in the specified folder
	void openTerminal(const QString& folder);
	// Calculates total size for the specified objects
	FilesystemObjectsStatistics calculateStatistics(Panel p, const std::vector<qulonglong> & hashes);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(Panel p, qulonglong dirHash);
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow(Panel p);

// Getters
	const CPanel& panel(Panel p) const;
	CPanel& panel(Panel p);
	const CPanel& otherPanel(Panel p) const;
	CPanel& otherPanel(Panel p);
	static Panel otherPanelPosition(Panel p);
	Panel activePanelPosition() const;
	const CPanel& activePanel() const;
	CPanel& activePanel();
	CPluginProxy& pluginProxy();
	bool itemHashExists(Panel p, qulonglong hash) const;
	CFileSystemObject itemByHash(Panel p, qulonglong hash) const;
	std::vector<CFileSystemObject> items (Panel p, const std::vector<qulonglong> &hashes) const;
	QString itemPath(Panel p, qulonglong hash) const;
	QString diskPath(int index) const;
	CFavoriteLocations& favoriteLocations();
	int currentDiskIndex(Panel p) const;

	CDiskEnumerator& diskEnumerator();

	// Returns hash of an item that was the last selected in the specified dir
	qulonglong currentItemInFolder(Panel p, const QString& dir) const;

private:
	void disksChanged() override;

	void saveDirectoryForCurrentDisk(Panel p);

private:
	static CController * _instance;
	CFavoriteLocations   _favoriteLocations;
	CPanel               _leftPanel, _rightPanel;
	CPluginProxy         _pluginProxy;
	CDiskEnumerator      _diskEnumerator;
	std::vector<IDiskListObserver*> _disksChangedListeners;
	Panel                _activePanel;
};

#endif // CCONTROLLER_H

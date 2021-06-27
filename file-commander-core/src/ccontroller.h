#pragma once

#include "fileoperationresultcode.h"
#include "cpanel.h"
#include "diskenumerator/cvolumeenumerator.h"
#include "plugininterface/cpluginproxy.h"
#include "favoritelocationslist/cfavoritelocations.h"
#include "filesearchengine/cfilesearchengine.h"

#include <functional>
#include <optional>
#include <utility>
#include <vector>

class CController final : public CVolumeEnumerator::IVolumeListObserver
{
public:
	// Volume list observer interface
	class IVolumeListObserver
	{
	public:
		virtual ~IVolumeListObserver() = default;
		virtual void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept = 0;
	};

	CController();
	static CController& get();

	void loadPlugins();

	void setPanelContentsChangedListener(Panel p, PanelContentsChangedListener * listener);
	void setVolumesChangedListener(IVolumeListObserver * listener);

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
	// A current volume has been switched
	std::pair<bool /*success*/, QString/*volume root path*/> switchToVolume(Panel p, uint64_t id);
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
	FileOperationResultCode createFolder(const QString& parentFolder, const QString& name);
	// Creates a file with a specified name at the specified parent folder
	FileOperationResultCode createFile(const QString& parentFolder, const QString& name);
	// Opens a terminal window in the specified folder
	void openTerminal(const QString & folder, bool admin = false);
	// Calculates total size for the specified objects
	FilesystemObjectsStatistics calculateStatistics(Panel p, const std::vector<qulonglong> & hashes);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(Panel p, qulonglong dirHash);
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow(Panel p);
	// Indicates that we need to move cursor (e. g. a folder is being renamed and we want to keep the cursor on it)
	// This method takes the current folder in the currently active panel
	void setCursorPositionForCurrentFolder(Panel panel, qulonglong newCurrentItemHash, const bool notifyUi = true);
	// Copies the full path of the currently selected item to clipboard
	void copyCurrentItemPathToClipboard();

// Threading
	void execOnWorkerThread(std::function<void()> task);
	void execOnUiThread(std::function<void ()> task, int tag = -1);

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


	std::vector<VolumeInfo> volumes() const;
	std::optional<VolumeInfo> currentVolumeInfo(Panel p) const;
	std::optional<VolumeInfo> volumeInfoForObject(const CFileSystemObject& object) const noexcept;
	std::optional<VolumeInfo> volumeInfoById(uint64_t id) const;

	CFavoriteLocations& favoriteLocations();
	CFileSearchEngine& fileSearchEngine();

	// Returns hash of an item that was the last selected in the specified dir
	qulonglong currentItemHashForFolder(Panel p, const QString& dir) const;
	qulonglong currentItemHash();
	CFileSystemObject currentItem();

private:
	void volumesChanged(bool drivesListOrReadinessChanged) noexcept override;

	QString volumePathById(uint64_t id) const;
	void saveDirectoryForCurrentVolume(Panel p);

private:
	static CController * _instance;
	CFavoriteLocations   _favoriteLocations;
	CFileSearchEngine    _fileSearchEngine;
	CPanel               _leftPanel;
	CPanel               _rightPanel;
	CPluginProxy         _pluginProxy;
	CVolumeEnumerator    _volumeEnumerator;
	std::vector<IVolumeListObserver*> _volumesChangedListeners;
	Panel                _activePanel = UnknownPanel;

	CWorkerThreadPool _workerThreadPool; // The thread used to execute tasks out of the UI thread
	CExecutionQueue   _uiQueue;      // The queue for actions that must be executed on the UI thread
};

#pragma once

#include "fileoperationresultcode.h"
#include "cpanel.h"
#include "diskenumerator/cvolumeenumerator.h"
#include "plugininterface/cpluginproxy.h"
#include "favoritelocationslist/cfavoritelocations.h"
#ifdef _WIN32
#include "plugininterface/wcx/cwcxpluginhost.h"
#else
#include "plugininterface/wcx/cwcxpluginhost_stub.h"
#endif

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class CController final : public CVolumeEnumerator::IVolumeListObserver, public PanelContentsChangedListener, public CurrentPathChangedListener
{
public:
	// Volume list observer interface
	class IVolumeListObserver
	{
	public:
		virtual ~IVolumeListObserver() = default;
		virtual void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept = 0;
		// The panel's current directory moved, so its highlighted drive button may need to change. The volume
		// list itself is unchanged; only side p's button selection and free-space label are refreshed.
		virtual void currentVolumeChanged(Panel p) noexcept = 0;
	};

	CController();
	~CController(); // Saves the panel state (incl. cursor positions) on graceful shutdown
	[[nodiscard]] static CController& get();

	void loadPlugins();

	void setPanelContentsChangedListener(Panel p, PanelContentsChangedListener * listener);
	void setCursorPositionListener(Panel p, CursorPositionListener * listener);
	void setVolumesChangedListener(IVolumeListObserver * listener);

// Notifications from UI
	void uiThreadTimerTick();
	// Persists both sides' history + visited-locations logs. Driven periodically by the UI (autosave) in addition to
	// the destructor, so a session that ends without unwinding (kill, power loss, debugger stop) doesn't lose the log.
	void saveHistory();

	// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
	void refreshPanelContents(Panel p);
	// Tab management. A tab is an independent CPanel; panel(p) returns the active tab's CPanel for side p.
	// Tabs are identified by tab ID, not by position: the underlying tab list may be reordered/resized freely
	// without invalidating an id a caller is holding onto.
	// Creates a new tab for side p showing 'path' and returns its id. Active by default; pass activate=false
	// to leave the currently active tab as is (e. g. for a background tab opened via middle-click).
	qulonglong addTab(Panel p, const QString& path, bool activate = true);
	// Closes the tab with id. Never removes the last tab (a panel always keeps >= 1 tab).
	void closeTab(Panel p, qulonglong tabId);
	// Makes tabId the active tab for side p (deactivates the previously active tab, activates the new one).
	void setActiveTab(Panel p, qulonglong tabId);
	// Moves the tab with id to newPosition within side p's tab list (e. g. to mirror a drag-reorder in the UI).
	// No-op if tabId is already at newPosition. newPosition must be a valid index (same semantics as QTabBar::tabMoved's 'to').
	void moveTabPosition(Panel p, qulonglong tabId, size_t newPosition);
	[[nodiscard]] int tabCount(Panel p) const;
	[[nodiscard]] qulonglong activeTabId(Panel p) const;
	[[nodiscard]] std::vector<qulonglong> tabIds(Panel p) const; // Tab ids in display order, for the UI to (re)build its tab strip
	[[nodiscard]] QString tabPath(Panel p, qulonglong tabId) const;
	[[nodiscard]] QString tabName(Panel p, qulonglong tabId) const; // The tab's folder name, for the tab label
	[[nodiscard]] const CPanel& tabById(Panel p, qulonglong tabId) const; // For read-only queries against non-active tabs (their lists are as of when they were last active)
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
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(Panel p, qulonglong dirHash);
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow(Panel p);
	// Indicates that we need to move cursor (e. g. a folder is being renamed and we want to keep the cursor on it)
	// This method takes the current folder in the currently active panel
	void setCursorPositionForCurrentFolder(Panel panel, qulonglong newCurrentItemHash, bool notifyUi = true);
	// Copies the full path of the currently selected item to clipboard
	void copyCurrentItemPathToClipboard();

// Threading
	void execOnWorkerThread(std::function<void()> task);
	void execOnUiThread(std::function<void ()> task, int tag = -1);

// Getters
	[[nodiscard]] const CPanel& panel(Panel p) const;
	[[nodiscard]] CPanel& panel(Panel p);
	[[nodiscard]] const CPanel& otherPanel(Panel p) const;
	[[nodiscard]] CPanel& otherPanel(Panel p);
	[[nodiscard]] static Panel otherPanelPosition(Panel p);
	[[nodiscard]] Panel activePanelPosition() const;
	[[nodiscard]] const CPanel& activePanel() const;
	[[nodiscard]] CPanel& activePanel();

	// Per-side, tab-independent log of visited folders (unlike a tab's own back/forward CHistoryList, this
	// survives tab close/open). Powers the path navigator's quick-revisit dropdown.
	[[nodiscard]] const CHistoryList<QString>& visitedLocations(Panel p) const;

	[[nodiscard]] CPluginProxy& pluginProxy();

	[[nodiscard]] bool itemHashExists(Panel p, qulonglong hash) const;
	[[nodiscard]] CFileSystemObject itemByHash(Panel p, qulonglong hash) const;
	[[nodiscard]] std::vector<CFileSystemObject> items(Panel p, const std::vector<qulonglong> &hashes) const;
	[[nodiscard]] QString itemPath(Panel p, qulonglong hash) const;


	[[nodiscard]] std::vector<VolumeInfo> volumes() const;
	[[nodiscard]] std::optional<VolumeInfo> currentVolumeInfo(Panel p) const;
	[[nodiscard]] std::optional<VolumeInfo> volumeInfoForObject(const CFileSystemObject& object) const noexcept;
	[[nodiscard]] std::optional<VolumeInfo> volumeInfoById(uint64_t id) const;

	[[nodiscard]] CFavoriteLocations& favoriteLocations();

	// Returns hash of an item that was the last selected in the specified dir
	[[nodiscard]] qulonglong currentItemHashForFolder(Panel p, const QString& dir) const;
	[[nodiscard]] qulonglong currentItemHash();
	[[nodiscard]] CFileSystemObject currentItem();

private:
	void volumesChanged(bool drivesListOrReadinessChanged) noexcept override;
	// Fired after a navigation changes side p's current directory, to refresh only its drive-button selection.
	void notifyCurrentVolumeChanged(Panel p) noexcept;

	[[nodiscard]] QString volumePathById(uint64_t id) const;
	void saveDirectoryForCurrentVolume(Panel p);

	// Each side holds a list of tabs; each tab is an independent CPanel. panel(p) returns the active tab.
	struct TabList {
		std::vector<std::unique_ptr<CPanel>> tabs;
		size_t activeTab = 0;
	};
	// Creates a CPanel for side p, wires its listeners, appends it as a new tab and returns it. Does NOT set a path.
	CPanel& createTab(Panel p);
	// Attaches all of side p's recorded contents/cursor listeners (plus the plugin engine) to a freshly created tab.
	void attachListenersToTab(Panel p, CPanel& tab);
	// Deactivates the currently active tab and activates tabId. Shared by addTab and setActiveTab; callers are
	// responsible for skipping the call when tabId is already the active tab.
	void switchActiveTab(Panel p, qulonglong tabId);
	// Returns tabId's position within side p's tab list, or nullopt if not found (should never happen for a live id).
	[[nodiscard]] std::optional<size_t> tabIndexById(Panel p, qulonglong tabId) const;

	// Persistence (centralized here; CPanel no longer touches settings).
	void restorePanelState(Panel p); // Rebuilds side p's tabs from settings (with migration from the legacy single-path keys)
	void savePanelState(Panel p);    // Writes side p's tab paths + active index + the active tab's path (deduplicated)
	void saveHistoryList(Panel p);   // Writes side p's active-tab back/forward history + visited-locations log; see saveHistory()

	// PanelContentsChangedListener: the controller listens to its own tabs only to persist on navigation.
	void onPanelContentsChanged(Panel p, FileListRefreshCause operation) override;
	// CurrentPathChangedListener: appends to the side's visited-locations log whenever any tab's directory changes.
	void onCurrentPathChanged(Panel p, const QString& newPath) override;
	// Diagnostic tripwire: after the addLatest in onCurrentPathChanged, verifies no entry was silently dropped from
	// the side's visited-locations log and fires a loud modal if one was. Needs the pre-add size and presence flag.
	void warnIfVisitedLocationDropped(Panel p, const QString& newPath, size_t sizeBefore, bool wasAlreadyLogged);

private:
	static CController * _instance;
	CFavoriteLocations   _favoriteLocations;
	// Shared worker pool for all panel tabs. Declared before _panels so it outlives the CPanels that post tasks to it.
	CWorkerThreadPool    _panelWorkerPool;
	std::array<TabList, 2> _panels;
	qulonglong             _nextTabId = 1; // 0 is reserved as "no tab"/invalid
	// Listeners attached to every tab of a side; recorded so tabs created later also get them.
	std::array<std::vector<PanelContentsChangedListener*>, 2> _panelContentsListeners;
	std::array<std::vector<CursorPositionListener*>, 2> _cursorPositionListeners;
	std::array<QString, 2> _lastSavedTabSignature; // Dedup key for savePanelState (avoids rewriting settings on every watcher refresh)
	std::array<CHistoryList<QString>, 2> _visitedLocations; // Per-side, tab-independent visited-folders log; see visitedLocations()
	CPluginProxy         _pluginProxy;
	CWcxPluginHost       _wcxHost;
	CVolumeEnumerator    _volumeEnumerator;
	std::vector<IVolumeListObserver*> _volumesChangedListeners;
	Panel                _activePanel = Panel::UnknownPanel;

	// Declared before the worker pool so it outlives every controller task that can enqueue UI work.
	CExecutionQueue   _uiQueue;
	CWorkerThreadPool _workerThreadPool; // The thread used to execute tasks out of the UI thread
};

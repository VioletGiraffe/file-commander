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
#include <shared_mutex>
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
	~CController();
	[[nodiscard]] static CController& get();

	// Closes plugin access to panels and UI dispatch, stops panel/volume producers, and releases callbacks while their
	// owners are still alive. Must be called exactly once; the empty tab lists are checked by ~CController.
	void shutdown();

	// The UI installs the menu builder; plugins reach it through their proxy. Both are main thread only.
	void setToolMenuEntryCreatorImplementation(const CPluginProxy::CreateToolMenuEntryImplementationType& implementation);
	void createToolMenuEntries(const std::vector<CPluginProxy::MenuTree>& menuTrees);

	// Plugin-facing feed of the active tab's committed listing, handed out by reference under the panel's lock.
	// Subscribing delivers what's already committed, so a subscriber to an idle panel isn't left waiting for an
	// event that will never come. Main thread only; the subscriber pointer is an identity token for unsubscribing.
	using PanelContentsSubscriber = std::function<void(Panel p, const QString& folder, const FileListHashMap& contents)>;
	void subscribeToPanelContents(const CPluginProxy* subscriber, PanelContentsSubscriber callback);
	void unsubscribeFromPanelContents(const CPluginProxy* subscriber);

	void setPanelContentsChangedListener(Panel p, PanelContentsChangedListener * listener);
	void setCurrentItemChangedListener(Panel p, CurrentItemChangedListener * listener);
	void setVolumesChangedListener(IVolumeListObserver * listener);

// Notifications from UI
	void uiThreadTimerTick();
	// Persists both sides' history + visited-locations logs. Driven periodically by the UI (autosave) in addition to
	// shutdown(), so a session that ends without unwinding (kill, power loss, debugger stop) doesn't lose the log.
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
	// Visible selection is UI-owned; mirror it into the plugin-facing panel snapshot.
	void selectionChanged(Panel p, const std::vector<qulonglong>& selectedItemsHashes);

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
	// Designates a new current item (e. g. a folder is being renamed and we want to keep the cursor on it).
	// Applies to whichever folder the given panel is currently showing.
	void setCurrentItemHashForCurrentFolder(Panel panel, qulonglong newCurrentItemHash, bool notifyUi = true);
	// Copies the full path of the currently selected item to clipboard
	void copyCurrentItemPathToClipboard();

// Threading
	// Templated so a callable goes straight into the target queue's own type erasure instead of through a std::function first
	template <typename Functor>
	void execOnUiThread(Functor&& f, int tag = -1)
	{
		std::shared_lock lock(_pluginAccessMutex);
		// Both sides are created and destroyed together; either one is sufficient as the controller-lifetime sentinel.
		if (_panels[(size_t)Panel::LeftPanel].tabs.empty())
			return;

		_uiQueue.enqueue(std::forward<Functor>(f), tag);
	}

	// Shared general-purpose pool. A caller whose lifetime is shorter than the controller's must tag its tasks
	// and retire the tag before it dies.
	[[nodiscard]] CThreadPool& threadPool();

// Getters
	// A side always has at least one tab, so these are valid until shutdown() destroys them all.
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

	// Mirror of the UI's selection, for the plugin API; the UI remains its owner.
	[[nodiscard]] std::vector<qulonglong> selectedItemsHashes(Panel p) const;
	[[nodiscard]] QString currentFolderPath(Panel p) const;
	[[nodiscard]] CFileSystemObject currentItem(Panel p) const;

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
	// Creates a CPanel for side p with the given id, wires its listeners, appends it as a new tab and returns it.
	// Does NOT set a path. The id must be one no other tab has ever had; see KEY_NEXT_TAB_ID.
	CPanel& createTab(Panel p, qulonglong id);
	// Attaches all of side p's recorded contents/cursor listeners to a freshly created tab.
	void attachListenersToTab(Panel p, CPanel& tab);
	// Deactivates the currently active tab and activates tabId. Shared by addTab and setActiveTab; callers are
	// responsible for skipping the call when tabId is already the active tab.
	void switchActiveTab(Panel p, qulonglong tabId);
	// Returns tabId's position within side p's tab list, or nullopt if not found (should never happen for a live id).
	[[nodiscard]] std::optional<size_t> tabIndexById(Panel p, qulonglong tabId) const;

	// Persistence (centralized here; CPanel no longer touches settings).
	void restorePanelState(Panel p); // Rebuilds side p's tabs from settings (with migration from the legacy single-path keys)
	void savePanelState(Panel p);    // Writes side p's tab records + active index + the active tab's path (deduplicated)
	void saveHistoryList(Panel p);   // Writes side p's active-tab back/forward history + visited-locations log; see saveHistory()
	// Hands side p's committed listing to every subscriber, or to none if the panel has no listing for its current view.
	void publishPanelContents(Panel p);

	// PanelContentsChangedListener: the controller listens to its own tabs only to persist on navigation.
	void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) override;
	void onPanelContentsInvalidated(Panel p, qulonglong tabId) override;
	// CurrentPathChangedListener: appends to the side's visited-locations log whenever any tab's directory changes.
	void onCurrentPathChanged(Panel p, const QString& newPath) override;
	// Diagnostic tripwire: after the addLatest in onCurrentPathChanged, verifies no entry was silently dropped from
	// the side's visited-locations log and fires a loud modal if one was. Needs the pre-add size and presence flag.
	void warnIfVisitedLocationDropped(Panel p, const QString& newPath, size_t sizeBefore, bool wasAlreadyLogged);

private:
	static CController * _instance;
	CFavoriteLocations   _favoriteLocations;
	// All panel tabs' file list enumeration and dir size calculation. Sized to the core count: that work parallelises
	// across tabs. Every task carries its CPanel's _taskTag, so ~CPanel retires its own without waiting for other tabs.
	// Declared before _panels so it outlives the CPanels that post tasks to it.
	CThreadPool             _panelWorkerPool;
	// General-purpose pool, do not use for tasks whose lifetime depends on CPanel.
	// Stopped in ~CController, not shutdown(), so its users can still run and retire tasks while they're alive.
	CThreadPool             _workerPool;
	std::array<TabList, 2> _panels;
	qulonglong             _nextTabId = 1; // 0 is reserved as "no tab"/invalid
	// Listeners attached to every tab of a side; recorded so tabs created later also get them.
	std::array<std::vector<PanelContentsChangedListener*>, 2> _panelContentsListeners;
	std::array<std::vector<CurrentItemChangedListener*>, 2> _currentItemChangedListeners;
	std::array<QString, 2> _lastSavedTabSignature; // Dedup key for savePanelState (avoids rewriting settings on every watcher refresh)
	std::array<CHistoryList<QString>, 2> _visitedLocations; // Per-side, tab-independent visited-folders log; see visitedLocations()
	// TODO: pushed in by the UI because the selection lives there; it should be pulled like everything else once
	// the core owns the selection. Protected by _pluginAccessMutex because plugins may query it from worker threads.
	std::array<std::vector<qulonglong>, 2> _selectedItemsHashes; // See selectedItemsHashes()
	CPluginProxy::CreateToolMenuEntryImplementationType _createToolMenuEntryImplementation;
	struct PanelContentsSubscription {
		const CPluginProxy* subscriber;
		PanelContentsSubscriber callback;
	};
	std::vector<PanelContentsSubscription> _panelContentsSubscriptions;
	CWcxPluginHost       _wcxHost;
	CVolumeEnumerator    _volumeEnumerator;
	std::vector<IVolumeListObserver*> _volumesChangedListeners;
	Panel                _activePanel = Panel::UnknownPanel; // Protected by _pluginAccessMutex
	// Plugin-facing panel queries and UI dispatch hold a shared lock. Tab/state mutations and shutdown() hold the
	// exclusive lock; shutdown establishes the empty-panel/unknown-active-panel postconditions before releasing it.
	mutable std::shared_mutex _pluginAccessMutex;

	CExecutionQueue   _uiQueue;
};

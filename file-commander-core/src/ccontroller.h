#pragma once

#include "fileoperationlaunch.h"
#include "fileoperationresultcode.h"
#include "cpanel.h"
#include "diskenumerator/cvolumeenumerator.h"
#include "favoritelocationslist/cfavoritelocations.h"
#include "shell/cshell.h"
#ifdef _WIN32
#include "wcx/cwcxpluginhost.h"
#else
#include "wcx/cwcxpluginhost_stub.h"
#endif

#include "fileoperations/inlinerename.h"
#include "fileoperations/usermessage.h"


#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

class CPluginProxy;

class CController final : public CVolumeEnumerator::IVolumeListObserver, public PanelContentsChangedListener, public CurrentPathChangedListener
{
public:
	// Volume list observer interface
	class IVolumeListObserver
	{
	public:
		virtual ~IVolumeListObserver() = default;
		virtual void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool significantChange) noexcept = 0;
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

	// Updates the list of files in the current directory this panel is viewing, and send the new state to UI.
	void refreshPanelContents(Panel p);
	// Whether the panels list hidden entries. Persisted; a change refreshes both sides.
	[[nodiscard]] bool showHiddenFiles() const;
	void setShowHiddenFiles(bool show);
#ifdef _WIN32
	// Refreshes each side's displayed panel that is on the drive just removed: the refresh moves it to an accessible folder.
	// Its watch was released before the removal and reports nothing.
	void refreshPanelsOnRemovedDrive(QChar driveLetter);
#endif
	// Tab management. A tab is an independent CPanel; panel(p) returns the active tab's CPanel for side p.
	// Tabs are identified by tab ID, not by position: the underlying tab list may be reordered/resized freely
	// without invalidating an id a caller is holding onto.
	// Creates a new tab for side p showing 'path' and returns its id. Active by default; pass activate=false
	// to leave the currently active tab as is (e. g. for a background tab opened via middle-click).
	// sortingSourceTabId: a tab of side p whose sorting the new tab takes; 0 for the default sorting.
	qulonglong addTab(Panel p, const QString& path, bool activate = true, qulonglong sortingSourceTabId = 0);
	// Activates side p's tab showing the folder, or creates one there with the active tab's sorting. Returns the tab's id.
	// Paths match after QDir::cleanPath, ignoring case on Windows only.
	qulonglong activateOrAddTabShowing(Panel p, const QString& folder);
	// Closes the tab with id. Never removes the last tab (a panel always keeps >= 1 tab).
	void closeTab(Panel p, qulonglong tabId);
	// Removes and returns the folder of side p's most recently closed tab; empty when there is none.
	[[nodiscard]] QString takeLastClosedTabPath(Panel p);
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
	struct ItemActivationResult
	{
		FileOperationResultCode code;
		QString errorText; // The reason for a failure, when known
		QString pathToLaunch; // A file or an app bundle, for the UI to start; empty otherwise
		bool isProgram = false; // pathToLaunch runs in its folder; otherwise it opens with its default application
	};
	// Enters a folder; a file or an app bundle is returned in pathToLaunch
	ItemActivationResult itemActivated(qulonglong itemHash, Panel p);
	// Shows the volume in side p: the other side's folder when it is on that volume, else the folder last visited there, else its root.
	// Returns the message to show when the volume is inaccessible.
	[[nodiscard]] std::optional<UserMessage> switchToVolume(Panel p, uint64_t id);
	// Focus is set to a panel
	void activePanelChanged(Panel p);
	// The tab's sorting is persisted with it
	void setTabSorting(Panel p, qulonglong tabId, FileListSortField field, Qt::SortOrder order);
	// Deselects a copy's sources only when it completed with nothing failed or skipped. No-op if the tab has been closed.
	void deselectCopiedItems(Panel p, qulonglong tabId, const std::vector<qulonglong>& sourceHashes, const OperationSummary& summary);

// Operations
	// Navigates specified panel up the directory tree
	void navigateUp(Panel p);
	// Go to the previous location from history, if any
	void navigateBack(Panel p);
	// Go to the next location from history, if any
	void navigateForward(Panel p);
	// Sets the specified path, if possible. Otherwise reverts to the previously set path
	FileOperationResultCode setPath(Panel p, const QString& path, FileListRefreshCause operation);
	// Opens the item's folder with the cursor on the item
	void goToItem(Panel p, const CFileSystemObject& item);
	// Goes to a path the user typed, resolved against side p's folder: see resolveUserPath(). Returns the message to show, if any.
	[[nodiscard]] std::optional<UserMessage> goToTypedPath(Panel p, QString text);

	// Create a folder or an empty file in side p's folder from the name the user typed, and put the cursor on the new entry.
	// Return the message to show; nullopt when the entry was created.
	// relativePath may be a chain ("a/b/c"): see createNewFolder().
	[[nodiscard]] std::optional<UserMessage> createFolder(Panel p, const QString& relativePath);
	[[nodiscard]] std::optional<UserMessage> createFile(Panel p, const QString& name);
	// Renames an item of side p's folder, and puts the cursor on it. fileReplacement as for inlineRename().
	// The message for the result: inlineRenameMessage().
	[[nodiscard]] InlineRenameResult renameItem(Panel p, qulonglong itemHash, const QString& newName, InlineRenameReplacement fileReplacement);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void displayDirSize(Panel p, qulonglong dirHash);
	// Moves the items to the trash on a worker thread. backend: QtTrash, or NativeShellTrash on macOS.
	// onFailures runs on the UI thread with the items the trash refused; not when it refused none, or after shutdown().
	void moveToTrash(QStringList paths, DeletionBackend backend, std::function<void(const std::vector<OsShell::TrashFailure>& failures)> onFailures);
	// Flattens the current directory and displays all its child files on one level
	void showAllFilesFromCurrentFolderAndBelow(Panel p);
	// Designates a new current item (e. g. a folder is being renamed and we want to keep the cursor on it).
	// Applies to whichever folder the given panel is currently showing.
	void setCurrentItemHashForCurrentFolder(Panel panel, qulonglong newCurrentItemHash, bool notifyUi = true);

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

	// The active tab's selection; safe to call from a plugin's worker thread
	[[nodiscard]] std::vector<qulonglong> selectedItemsHashes(Panel p) const;
	[[nodiscard]] QString currentFolderPath(Panel p) const;
	[[nodiscard]] CFileSystemObject currentItem(Panel p) const;

	[[nodiscard]] CFileSystemObject itemByHash(Panel p, qulonglong hash) const;


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
	void volumesChanged(bool significantChange) noexcept override;
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

private:
	static CController * _instance;
	CFavoriteLocations   _favoriteLocations;
	// Both pools are declared before _panels so they outlive the CPanels that post tasks to them.
	// A CPanel's tasks in either pool carry its _taskTag, so ~CPanel retires its own without waiting for other tabs.

	// All panel tabs' file list enumeration, and nothing else, so other work cannot delay a listing.
	// A fixed thread count: listing threads mostly wait on disk or network I/O, so the CPU core count is irrelevant.
	CThreadPool             _listingThreadPool;
	// General-purpose pool: plugin tasks and panel folder size calculations.
	// Stopped in ~CController, not shutdown(), so its users can still run and retire tasks while they're alive.
	CThreadPool             _workerPool;
	std::array<TabList, 2> _panels;
	qulonglong             _nextTabId = 1; // 0 is reserved as "no tab"/invalid
	std::array<std::vector<QString>, 2> _closedTabPaths; // Most recent last; see takeLastClosedTabPath()
	// Listeners attached to every tab of a side; recorded so tabs created later also get them.
	std::array<std::vector<PanelContentsChangedListener*>, 2> _panelContentsListeners;
	std::array<std::vector<CurrentItemChangedListener*>, 2> _currentItemChangedListeners;
	std::array<QString, 2> _lastSavedTabSignature; // The last state savePanelState wrote successfully: avoids rewriting settings on every watcher refresh
	std::array<CHistoryList<QString>, 2> _visitedLocations; // Per-side, tab-independent visited-folders log; see visitedLocations()
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

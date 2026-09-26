#pragma once

#include "columns.h"
#include "filelistwidget/cfilelistview.h"

#include "ccontroller.h"


DISABLE_COMPILER_WARNINGS
#include <QItemSelection>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>

namespace Ui {
class CPanelWidget;
}

class QItemSelectionModel;
class QStandardItem;
class QUrl;

class CFileListModel;
class CFileListFilterDialog;
class CShellOperationRunner;
struct InlineRenameResult;


class CPanelWidget final : public QWidget,
					 private CController::IVolumeListObserver,
					 public PanelContentsChangedListener,
					 private FileListReturnPressOrDoubleClickObserver,
					 public CurrentItemChangedListener,
					 public IconsChangedListener
{
	Q_OBJECT

public:
	explicit CPanelWidget(QWidget *parent = nullptr) noexcept;
	~CPanelWidget() noexcept  override;

	void init(CController* controller, CShellOperationRunner& shellOperations);

	void setFocusToFileList();

	// Persists every tab's sort and column layout. Called at shutdown: nothing else writes this store.
	void saveTabViewStates() const;

	[[nodiscard]] QString currentDirPathNative() const;

	[[nodiscard]] Panel panelPosition() const;
	void initPanel(Panel p);

	// Tabs (this side). The QTabBar and the per-tab models (_tabs) are kept position-aligned with each
	// other; CController's tabs are addressed by tab ID, recovered from a QTabBar position via tabIdAt().
	void createNewTab();          // New tab showing the current folder, switched to
	void closeCurrentTab();       // Closes the active tab (no-op when it's the only one)
	void switchToNextTab();
	void switchToPreviousTab();
	void openCurrentItemInNewTab(); // Ctrl+Up: opens the folder under the cursor in a new tab (no-op if it's not a folder)
	void reopenLastClosedTab();   // Reopens this side's most recently closed tab as a new tab at the end (path only, history is not preserved)
	void duplicateCurrentTab();   // Opens a background tab with the same path (menu counterpart of the tab context menu's "Duplicate tab")
	void closeAllTabsExceptCurrent();

	// CPanel observers
	void onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation) override;
	void onPanelContentsInvalidated(Panel p, qulonglong tabId) override;
	void onPreciseIconsAvailable(const std::vector<qulonglong>& objectHashes) override;
	void onAllIconsInvalidated() override;

	[[nodiscard]] CFileListView* fileListView() const;
	[[nodiscard]] QAbstractItemModel* model() const;

// Selection
	[[nodiscard]] std::vector<qulonglong> selectedItemsHashes(bool onlyHighlightedItems = false) const;
	[[nodiscard]] qulonglong currentItemHash() const;
	void invertSelection();
	void moveCursorToFirstFile(); // Scrolls past the folders (they always sort on top) and puts the cursor on the first file
	void toRoot();
	void copySelectedItemsPathsToClipboard() const; // Paths of all the selected items, or of the item under cursor if there's no selection

	void onSettingsChanged();

	void showFilterEditor();

signals:
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void currentItemChangedSignal(Panel p, qulonglong itemHash);
	// model() returns the newly active tab's model from here on
	void activeTabChanged();

protected:
	bool eventFilter(QObject * object , QEvent * e) override;

private slots:
	void showContextMenuForItems(QPoint pos);
	void showContextMenuForDisk(QPoint pos);
	void showContextMenuForTab(QPoint pos);
	void onSpacePressed();
	void invertCurrentItemSelection();
	void driveButtonClicked();
	void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
	void currentItemChanged(const QModelIndex& current, const QModelIndex& previous);
	void renameItem(qulonglong hash, const QString& newName);
	void showFavoriteLocationsMenu(QPoint pos);
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(const QString& keyText, int key, Qt::KeyboardModifiers modifiers);
	void filterTextEdited(const QString& filterText);
	void filterTextConfirmed(const QString& filterText);
	void copySelectionToClipboard() const;
	void cutSelectionToClipboard() const;
	void pasteSelectionFromClipboard(bool specialPaste = false);
	void pathFromHistoryActivated(QString path);
	void onItemMiddleClicked(const QModelIndex& index); // Middle-click: opens the folder in a new tab (no-op if it's not a folder)

private:
	// Returns true if it reset the model
	bool fillFromList(FileListRefreshCause operation);
	void fillFromPanel(FileListRefreshCause operation);
	void fillHistory();
	void updateInfoLabel();
	// The selected rows, or with none selected and onlyHighlightedItems false, the row under the cursor; never [..]
	[[nodiscard]] QModelIndexList selectedItemIndexes(bool onlyHighlightedItems = false) const;

// Callbacks
	bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item) override;
	void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept override;
	void currentVolumeChanged(Panel p) noexcept override;
	void onCurrentItemChanged(Panel p, qulonglong tabId, const QString& folder, qulonglong currentItemHash) override;

// Internal methods
	// Copies or moves the local files among urls; an empty destinationPath means the current folder
	bool dropUrls(const QList<QUrl>& urls, Qt::DropAction action, const QString& destinationPath);
	// The items of selectedItemIndexes(); cut: a paste moves them instead of copying
	void putSelectionOnClipboard(bool cut) const;

	void updateCurrentVolumeButtonAndInfoLabel();

	// The message for every inline-rename outcome in which the entry was not renamed.
	void reportFailedRename(const InlineRenameResult& result, const QString& oldName, const QString& newName);

	// Saves the image as a new file in the current folder; tells the user if that fails
	void pasteImage(const QImage& image, bool lossyCompression);

// Tab helpers (UI side; _tabs is position-aligned with the QTabBar; CController is addressed by tab ID, see tabIdAt())
	struct PanelTab {
		CFileListModel* model = nullptr;
		QItemSelectionModel* selectionModel = nullptr;
		QByteArray headerState; // This tab's own column widths/order/visibility (sort indicator bits in here are ignored - the model owns the sort)
		std::optional<uint64_t> navigationId; // CPanel::navigationId() of the listing in the model; empty while it holds none
		CFileListView::ScrollPosition scrollPosition; // Saved while the tab is in the background
	};
	// A tab's persisted appearance. The defaults are what a tab gets with nothing stored for it.
	struct TabViewState {
		int sortColumn = ExtColumn;
		Qt::SortOrder sortOrder = Qt::AscendingOrder;
		QByteArray headerState;
	};
	// Wires a model and its selection model into an already-emplaced tab (not shown yet). The tab's sort
	// is independent from every other tab's from here on, so viewState only seeds it; see activateTab.
	void populateTabModels(PanelTab& tab, const TabViewState& viewState);
	[[nodiscard]] TabViewState viewStateOfTab(int index) const;  // What tab 'index' looks like right now
	[[nodiscard]] std::vector<std::pair<qulonglong, TabViewState>> loadTabViewStates() const; // Persisted state by tab id; empty when nothing is stored
	[[nodiscard]] TabViewState viewStateFromLegacyHeaderBlob() const; // Migration: settings that predate the per-tab store held one blob per side
	[[nodiscard]] qulonglong tabIdAt(int index) const; // The tab ID stored as this QTabBar position's tab data
	[[nodiscard]] bool displaysTab(Panel p, qulonglong tabId) const; // Filters the per-tab CPanel notifications down to the tab on screen
	void activateTab(int index);                   // Points the shared view at tab 'index's models, restoring its own column widths, sort and scroll position
	void saveActiveTabViewState();                 // Stores what activateTab() restores, before the view shows another tab
	void onTabBarCurrentChanged(int index);
	void onTabBarCloseRequested(int index);
	void onTabBarTabMoved(int from, int to);        // Drag-reorder: mirrors the QTabBar's move into _tabs and CController
	void closeTabById(qulonglong id);       // Closes whichever tab currently holds id (re-resolves its position fresh); shared by onTabBarCloseRequested and closeAllOtherTabs
	void updateTabBarVisibility();                 // The bar stays hidden while there's only one tab
	void updateTabText(int index);
	[[nodiscard]] QString tabToolTipText(int index) const; // Tab's full path + folder contents stats, composed on hover
	// Shared by createNewTab() and openCurrentItemInNewTab()/onItemMiddleClicked(); activate=false keeps the new tab
	// in the background. viewState is the new tab's appearance: the active tab's, except when duplicating another tab.
	void openPathInNewTab(const QString& path, bool activate, const TabViewState& viewState);
	void tryOpenItemInNewTab(const QModelIndex& index, bool activate); // Opens the item in a new tab if it's a folder ([..] opens the parent)
	void duplicateTab(int index);       // Tab context menu: opens a new tab showing the same path as tab 'index'
	void closeAllOtherTabs(int index);  // Tab context menu: closes every tab except 'index'
	void switchToTabByPosition(int position); // Ctrl+1..9: jumps to the tab at this position (no-op if it doesn't exist)

private:
	CFileListFilterDialog          * _filterDialog = nullptr;
	std::vector<CFileSystemObject>  _disks;
	QString                         _currentVolumePath;
	Ui::CPanelWidget              * ui = nullptr;
	CController                   * _controller = nullptr;
	CShellOperationRunner         * _shellOperations = nullptr;
	// The active tab's models (also held in _tabs[_activeTab]); kept as members so the rest of the widget stays tab-agnostic.
	QItemSelectionModel           * _selectionModel = nullptr;
	CFileListModel                * _model = nullptr;
	std::vector<PanelTab>           _tabs;
	std::vector<QString>            _recentlyClosedTabsPaths; // LIFO for reopenLastClosedTab()
	int                             _activeTab = -1;
	qulonglong                      _lastSignalledCurrentItemHash = 0;
	Panel                           _panelPosition = Panel::UnknownPanel;
};

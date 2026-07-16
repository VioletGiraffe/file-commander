#pragma once

#include "ccontroller.h"
#include "filelistwidget/cfilelistview.h"

DISABLE_COMPILER_WARNINGS
#include <QItemSelection>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CPanelWidget;
}

class QItemSelectionModel;
class QSortFilterProxyModel;
class QStandardItem;

class CFileListModel;
class CFileListSortFilterProxyModel;
class CFileListFilterDialog;


class CPanelWidget final : public QWidget,
					 private CController::IVolumeListObserver,
					 public PanelContentsChangedListener,
					 private FileListReturnPressOrDoubleClickObserver,
					 public CursorPositionListener
{
	Q_OBJECT

public:
	explicit CPanelWidget(QWidget *parent = nullptr) noexcept;
	~CPanelWidget() noexcept  override;

	void init(CController* controller);

	void setFocusToFileList();

	[[nodiscard]] QByteArray savePanelState() const;
	bool restorePanelState(const QByteArray& state);

	[[nodiscard]] QByteArray savePanelGeometry() const;
	bool restorePanelGeometry(const QByteArray& state);

	[[nodiscard]] QString currentDirPathNative() const;

	[[nodiscard]] Panel panelPosition() const;
	void initPanel(Panel p);

	// Tabs (this side). The QTabBar and the per-tab model triplets (_tabs) are kept position-aligned with each
	// other; CController's tabs are addressed by tab ID, recovered from a QTabBar position via tabIdAt().
	void createNewTab();          // New tab showing the current folder, switched to
	void closeCurrentTab();       // Closes the active tab (no-op when it's the only one)
	void switchToNextTab();
	void switchToPreviousTab();
	void openCurrentItemInNewTab(); // Ctrl+Up: opens the folder under the cursor in a new tab (no-op if it's not a folder)
	void reopenLastClosedTab();   // Reopens this side's most recently closed tab as a new tab at the end (path only, history is not preserved)
	void duplicateCurrentTab();   // Opens a background tab with the same path (menu counterpart of the tab context menu's "Duplicate tab")
	void closeAllTabsExceptCurrent();

	// Returns the list of items added to the view
	void fillFromList(FileListRefreshCause operation);
	void fillFromPanel(const CPanel& panel, FileListRefreshCause operation);

	// CPanel observers
	void onPanelContentsChanged(Panel p, FileListRefreshCause operation) override;

	[[nodiscard]] CFileListView* fileListView() const;
	[[nodiscard]] QAbstractItemModel* model() const;
	[[nodiscard]] QSortFilterProxyModel* sortModel() const;

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
	void renameItem(qulonglong hash, QString newName);
	void showFavoriteLocationsMenu(QPoint pos);
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(const QString& keyText, int key, Qt::KeyboardModifiers modifiers);
	void filterTextEdited(const QString& filterText);
	void filterTextConfirmed(const QString& filterText);
	void copySelectionToClipboard() const;
	void cutSelectionToClipboard() const;
	void pasteSelectionFromClipboard(bool specialPaste = false);
	void pathFromHistoryActivated(QString path);
	void onItemMiddleClicked(const QModelIndex& sortModelIndex); // Middle-click: opens the folder in a new tab (no-op if it's not a folder)

private:
	void fillHistory();
	void updateInfoLabel(const std::vector<qulonglong>& selection);

// Callbacks
	bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item) override;
	void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept override;
	void setCursorToItem(const QString& folder, qulonglong currentItemHash) override;

// Internal methods
	[[nodiscard]] qulonglong hashBySortModelIndex(const QModelIndex& index) const;
	[[nodiscard]] QModelIndex indexByHash(qulonglong hash, bool logFailures = false) const;

	void updateCurrentVolumeButtonAndInfoLabel();

	[[nodiscard]] bool pasteImage(const QImage& image, bool lossyCompression);

// Tab helpers (UI side; _tabs is position-aligned with the QTabBar; CController is addressed by tab ID, see tabIdAt())
	struct PanelTab {
		CFileListModel* model = nullptr;
		CFileListSortFilterProxyModel* sortModel = nullptr;
		QItemSelectionModel* selectionModel = nullptr;
		QByteArray headerState; // This tab's own column widths/order/visibility (sort indicator bits in here are ignored - sortModel owns the sort)
	};
	void populateTriplet(PanelTab& tab);            // Wires a model / sort-proxy / selection-model trio into an already-emplaced tab (not shown yet)
	[[nodiscard]] qulonglong tabIdAt(int index) const; // The tab ID stored as this QTabBar position's tab data
	void activateTab(int index);                   // Points the shared view at tab 'index's triplet, restoring its own column widths and sort
	void onTabBarCurrentChanged(int index);
	void onTabBarCloseRequested(int index);
	void onTabBarTabMoved(int from, int to);        // Drag-reorder: mirrors the QTabBar's move into _tabs and CController
	void closeTabById(qulonglong id);       // Closes whichever tab currently holds id (re-resolves its position fresh); shared by onTabBarCloseRequested and closeAllOtherTabs
	void updateTabBarVisibility();                 // The bar stays hidden while there's only one tab
	void updateTabText(int index);
	[[nodiscard]] QString tabToolTipText(int index) const; // Tab's full path + folder contents stats, composed on hover
	void openPathInNewTab(const QString& path, bool activate = true); // Shared by createNewTab() and openCurrentItemInNewTab()/onItemMiddleClicked(); activate=false keeps the new tab in the background
	void tryOpenItemInNewTab(const QModelIndex& sortModelIndex, bool activate); // Opens the item in a new tab if it's a folder ([..] opens the parent)
	void duplicateTab(int index);       // Tab context menu: opens a new tab showing the same path as tab 'index'
	void closeAllOtherTabs(int index);  // Tab context menu: closes every tab except 'index'
	void switchToTabByPosition(int position); // Ctrl+1..9: jumps to the tab at this position (no-op if it doesn't exist)

private:
	CFileListFilterDialog          * _filterDialog = nullptr;
	std::vector<CFileSystemObject>  _disks;
	QString                         _currentVoumePath;
	QString                         _directoryCurrentlyBeingDisplayed;
	Ui::CPanelWidget              * ui = nullptr;
	CController                   * _controller = nullptr;
	// The active tab's triplet (also held in _tabs[_activeTab]); kept as members so the rest of the widget stays tab-agnostic.
	QItemSelectionModel           * _selectionModel = nullptr;
	CFileListModel                * _model = nullptr;
	CFileListSortFilterProxyModel * _sortModel = nullptr;
	std::vector<PanelTab>           _tabs;
	std::vector<QString>            _recentlyClosedTabsPaths; // LIFO for reopenLastClosedTab()
	int                             _activeTab = -1;
	Panel                           _panelPosition = Panel::UnknownPanel;

	QModelIndex _previousCurrentItem;
};

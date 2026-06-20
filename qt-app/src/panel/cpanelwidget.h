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
	void setPanelPosition(Panel p);

	// Tabs (this side). These keep the QTabBar, the per-tab model triplets, and CController's tab list index-aligned.
	void createNewTab();          // New tab showing the current folder, switched to
	void closeCurrentTab();       // Closes the active tab (no-op when it's the only one)
	void switchToNextTab();
	void switchToPreviousTab();

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
	void onSpacePressed();
	void invertCurrentItemSelection();
	void driveButtonClicked();
	void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
	void currentItemChanged(const QModelIndex& current, const QModelIndex& previous);
	void renameItem(qulonglong hash, QString newName);
	void toRoot();
	void showFavoriteLocationsMenu(QPoint pos);
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(const QString& keyText, int key, Qt::KeyboardModifiers modifiers);
	void filterTextEdited(const QString& filterText);
	void filterTextConfirmed(const QString& filterText);
	void copySelectionToClipboard() const;
	void cutSelectionToClipboard() const;
	void pasteSelectionFromClipboard(bool specialPaste = false);
	void pathFromHistoryActivated(QString path);

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

// Tab helpers (UI side; index-aligned with CController's tabs and the QTabBar)
	struct PanelTab {
		CFileListModel* model = nullptr;
		CFileListSortFilterProxyModel* sortModel = nullptr;
		QItemSelectionModel* selectionModel = nullptr;
	};
	[[nodiscard]] PanelTab createModelTriplet();   // Creates and wires a model / sort-proxy / selection-model trio (not shown yet)
	void activateTab(int index);                   // Points the shared view at tab 'index's triplet, preserving column widths
	void onTabBarCurrentChanged(int index);
	void onTabBarCloseRequested(int index);
	void updateTabBarVisibility();                 // The bar stays hidden while there's only one tab
	void updateTabText(int index);

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
	int                             _activeTab = -1;
	Panel                           _panelPosition = Panel::UnknownPanel;

	QModelIndex _previousCurrentItem;
};

#pragma once

#include "ccontroller.h"
#include "filelistwidget/cfilelistview.h"
#include "filelistwidget/cfilelistfilterdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QItemSelection>
#include <QShortcut>
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
class CFileListView;


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

	// Returns the list of items added to the view
	void fillFromList(const FileListHashMap& items, FileListRefreshCause operation);
	void fillFromPanel(const CPanel& panel, FileListRefreshCause operation);

	// CPanel observers
	void panelContentsChanged(Panel p, FileListRefreshCause operation) override;

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
	void calcDirectorySize();
	void invertCurrentItemSelection();
	void driveButtonClicked();
	void selectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
	void currentItemChanged(const QModelIndex& current, const QModelIndex& previous);
	void renameItem(qulonglong hash, QString newName);
	void toRoot();
	void showFavoriteLocationsMenu(QPoint pos);
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(const QString& keyText, int key, Qt::KeyboardModifiers modifiers);
	void filterTextChanged(const QString& filterText);
	void copySelectionToClipboard() const;
	void cutSelectionToClipboard() const;
	void pasteSelectionFromClipboard();
	void pathFromHistoryActivated(const QString& path);

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

	[[nodiscard]] bool pasteImage(const QImage& image);

private:
	CFileListFilterDialog           _filterDialog;
	std::vector<CFileSystemObject>  _disks;
	QString                         _currentVoumePath;
	QString                         _directoryCurrentlyBeingDisplayed;
	Ui::CPanelWidget              * ui = nullptr;
	CController                   * _controller = nullptr;
	QItemSelectionModel           * _selectionModel = nullptr;
	CFileListModel                * _model = nullptr;
	CFileListSortFilterProxyModel * _sortModel = nullptr;
	Panel                           _panelPosition = Panel::UnknownPanel;

	QShortcut                       _calcDirSizeShortcut;
	QShortcut                       _selectCurrentItemShortcut;
	QShortcut                       _copyShortcut;
	QShortcut                       _cutShortcut;
	QShortcut                       _pasteShortcut;

	QModelIndex _previousCurrentItem;
};

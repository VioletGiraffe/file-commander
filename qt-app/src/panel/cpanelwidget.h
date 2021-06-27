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
	explicit CPanelWidget(QWidget *parent = nullptr);
	~CPanelWidget() override;

	void init(CController* controller);

	void setFocusToFileList();

	QByteArray savePanelState() const;
	bool restorePanelState(const QByteArray& state);

	QByteArray savePanelGeometry() const;
	bool restorePanelGeometry(const QByteArray& state);

	QString currentDirPathNative() const;

	Panel panelPosition() const;
	void setPanelPosition(Panel p);

	// Returns the list of items added to the view
	void fillFromList(const std::map<qulonglong, CFileSystemObject>& items, FileListRefreshCause operation);
	void fillFromPanel(const CPanel& panel, FileListRefreshCause operation);

	// CPanel observers
	void panelContentsChanged(Panel p, FileListRefreshCause operation) override;
	void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress, const QString& currentDir) override;

	CFileListView * fileListView() const;
	QAbstractItemModel* model() const;
	QSortFilterProxyModel* sortModel() const;

// Selection
	std::vector<qulonglong> selectedItemsHashes(bool onlyHighlightedItems = false) const;
	qulonglong currentItemHash() const;
	void invertSelection();

	void onSettingsChanged();

signals:
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void currentItemChangedSignal(Panel p, qulonglong itemHash);
	void fileListViewKeyPressedSignal(CPanelWidget* panelWidget, QString keyText, int key, Qt::KeyboardModifiers modifiers);

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
	void itemNameEdited(qulonglong hash, QString newName);
	void toRoot();
	void showFavoriteLocationsMenu(QPoint pos);
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(QString keyText, int key, Qt::KeyboardModifiers modifiers);
	void showFilterEditor();
	void filterTextChanged(QString filterText);
	void copySelectionToClipboard() const;
	void cutSelectionToClipboard() const;
	void pasteSelectionFromClipboard();
	void pathFromHistoryActivated(QString path);

private:
	void fillHistory();
	void updateInfoLabel(const std::vector<qulonglong>& selection);

// Callbacks
	bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item) override;
	void volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept override;
	void setCursorToItem(const QString& folder, qulonglong currentItemHash) override;

// Internal methods
	qulonglong hashBySortModelIndex(const QModelIndex& index) const;
	QModelIndex indexByHash(const qulonglong hash, bool logFailures = false) const;

	void updateCurrentVolumeButtonAndInfoLabel();

	bool pasteImage(const QImage& image);

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
	Panel                           _panelPosition = UnknownPanel;

	QShortcut                       _calcDirSizeShortcut;
	QShortcut                       _selectCurrentItemShortcut;
	QShortcut                       _showFilterEditorShortcut;
	QShortcut                       _copyShortcut;
	QShortcut                       _cutShortcut;
	QShortcut                       _pasteShortcut;

	QModelIndex _previousCurrentItem;
};

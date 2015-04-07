#ifndef CPANELWIDGET_H
#define CPANELWIDGET_H

#include "../QtAppIncludes"

#include "ccontroller.h"
#include "filelistwidget/cfilelistview.h"
#include "filelistwidget/cfilelistfilterdialog.h"

namespace Ui {
class CPanelWidget;
}

class QItemSelectionModel;
class CFileListModel;
class QStandardItem;
class CFileListSortFilterProxyModel;
class CFileListView;


class CPanelWidget : public QWidget, private CController::IDiskListObserver, public PanelContentsChangedListener, private FileListReturnPressOrDoubleClickObserver
{
	Q_OBJECT

public:
	explicit CPanelWidget(QWidget *parent = 0);
	~CPanelWidget();

	void setFocusToFileList();

	QByteArray savePanelState() const;
	bool restorePanelState(QByteArray state);

	QByteArray savePanelGeometry() const;
	bool restorePanelGeometry(QByteArray state);

	QString currentDir() const;

	Panel panelPosition() const;
	void setPanelPosition(Panel p);

	// Returns the list of items added to the view
	void fillFromList(const std::vector<CFileSystemObject>& items, bool sameDirAsPrevious, FileListRefreshCause operation);
	void fillFromPanel(const CPanel& panel, FileListRefreshCause operation);

	void panelContentsChanged(Panel p, FileListRefreshCause operation) override;

	CFileListView * fileListView() const;
	QAbstractItemModel* model() const;
	QSortFilterProxyModel* sortModel() const;

// Selection
	std::vector<qulonglong> selectedItemsHashes(bool onlyHighlightedItems = false) const;
	qulonglong currentItemHash() const;
	void invertSelection();

signals:
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void currentItemChanged(Panel p, qulonglong itemHash);
	void fileListViewKeyPressedSignal(CPanelWidget* panelWidget, QString keyText, int key, Qt::KeyboardModifiers modifiers);

protected:
	bool eventFilter(QObject * object , QEvent * e) override;

private slots:
	void showContextMenuForItems(QPoint pos);
	void showContextMenuForDisk(QPoint pos);
	void calcDirectorySize();
	void invertCurrentItemSelection();
	void driveButtonClicked();
	void selectionChanged(QItemSelection selected, QItemSelection deselected);
	void currentItemChanged(QModelIndex current, QModelIndex previous);
	void itemNameEdited(qulonglong hash, QString newName);
	void toRoot();
	void showFavoriteLocationsMenu();
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
	void disksChanged(QList<QStorageInfo> drives, Panel p, int currentDriveIndex) override;

// Internal methods
	qulonglong hashByItemIndex(const QModelIndex& index) const;
	qulonglong hashByItemRow(const int row) const;
	QModelIndex indexByHash(const qulonglong hash) const;

private:
	CFileListFilterDialog           _filterDialog;
	std::vector<CFileSystemObject>  _disks;
	QString                         _currentDisk;
	QString                         _directoryCurrentlyBeingDisplayed;
	Ui::CPanelWidget              * ui;
	CController                   & _controller;
	QItemSelectionModel           * _selectionModel;
	CFileListModel                * _model;
	CFileListSortFilterProxyModel * _sortModel;
	Panel			                _panelPosition;

	QShortcut                       _calcDirSizeShortcut;
	QShortcut                       _selectCurrentItemShortcut;
	QShortcut                       _showFilterEditorShortcut;
	QShortcut                       _copyShortcut;
	QShortcut                       _cutShortcut;
	QShortcut                       _pasteShortcut;
};

#endif // CPANELWIDGET_H

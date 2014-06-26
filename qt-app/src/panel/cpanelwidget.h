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
	CPanelWidget(QWidget *parent = 0);
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
	void fillFromList(const std::vector<CFileSystemObject>& items, bool sameDirAsPrevious);
	void fillFromPanel(const CPanel& panel);

	std::vector<qulonglong> selectedItemsHashes(bool onlyHighlightedItems = false) const;
	qulonglong currentItemHash() const;

	void panelContentsChanged(Panel p) override;

	CFileListView * fileListView() const;
	QAbstractItemModel* model() const;

signals:
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void focusReceived(CPanelWidget * panel);
	void folderPathSet(QString newPath, const CPanelWidget * panel);
	void itemNameEdited(Panel panel, qulonglong hash, QString newName);
	void fileListViewKeyPressedSignal(CPanelWidget* panelWidget, QString keyText, int key, Qt::KeyboardModifiers modifiers);

protected:
	bool eventFilter(QObject * object , QEvent * e) override;

private slots:
	void showContextMenuForItems(QPoint pos);
	void showContextMenuForDisk(QPoint pos);
	void onFolderPathSet();
	void calcDirectorySize();
	void invertCurrentItemSelection();
	void driveButtonClicked();
	void selectionChanged(QItemSelection selected, QItemSelection deselected);
	void currentItemChanged(QModelIndex current, QModelIndex previous);
	void itemNameEdited(qulonglong hash, QString newName);
	void showHistory();
	void toRoot();
	void showFavoriteLocationsMenu();
	void showFavoriteLocationsEditor();
	void fileListViewKeyPressed(QString keyText, int key, Qt::KeyboardModifiers modifiers);
	void showFilterEditor();

private:
// Callbacks
	bool fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item) override;
	void disksChanged(std::vector<CDiskEnumerator::Drive> drives, Panel p, size_t currentDriveIndex) override;

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
};

#endif // CPANELWIDGET_H

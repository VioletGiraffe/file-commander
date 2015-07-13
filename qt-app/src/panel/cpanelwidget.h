#ifndef CPANELWIDGET_H
#define CPANELWIDGET_H

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
	void disksChanged(const std::vector<CDiskEnumerator::DiskInfo>& drives, Panel p) override;

// Internal methods
	qulonglong hashByItemIndex(const QModelIndex& index) const;
	qulonglong hashByItemRow(const int row) const;
	QModelIndex indexByHash(const qulonglong hash) const;

	void updateCurrentDiskButton();

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
	Panel                           _panelPosition;

	QShortcut                       _calcDirSizeShortcut;
	QShortcut                       _selectCurrentItemShortcut;
	QShortcut                       _showFilterEditorShortcut;
	QShortcut                       _copyShortcut;
	QShortcut                       _cutShortcut;
	QShortcut                       _pasteShortcut;
};

#endif // CPANELWIDGET_H

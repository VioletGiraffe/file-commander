#ifndef CPANELWIDGET_H
#define CPANELWIDGET_H

#include "../QtAppIncludes"

#include "ccontroller.h"

namespace Ui {
class CPanelWidget;
}

class QItemSelectionModel;
class CFileListModel;
class QStandardItem;
class CFileListSortFilterProxyModel;


class CPanelWidget : public QWidget, private CController::IDiskListObserver, public PanelContentsChangedListener
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
	void fillFromList(const std::vector<CFileSystemObject>& items);
	void fillFromPanel(const CPanel& panel);

	std::vector<uint> selectedItemsHashes() const;

	virtual void panelContentsChanged(Panel p);

signals:
	void itemActivated(uint hash, CPanelWidget * panel);
	void backSpacePressed(CPanelWidget * panel);
	void stepBackRequested(CPanelWidget * panel);
	void stepForwardRequested(CPanelWidget * panel);
	void focusReceived(CPanelWidget * panel);
	void folderPathSet(QString newPath, const CPanelWidget * panel);

protected:
	virtual bool eventFilter (QObject * object , QEvent * e);

private slots:
	void itemActivatedSlot(QModelIndex item);
	void showContextMenuForItems(QPoint pos);
	void showContextMenuForDisk(QPoint pos);
	void onFolderPathSet();
	void calcDirectorySize();
	void invertCurrentItemSelection();
	void driveButtonClicked();

private:
	virtual void disksChanged(std::vector<CDiskEnumerator::Drive> drives, Panel p, size_t currentDriveIndex);

	uint hashByItemIndex(const QModelIndex& index) const;
	uint hashByItemRow(const int row) const;
	QModelIndex indexByHash(const uint hash) const;

private:
	QString                         _currentPath;
	std::vector<CFileSystemObject>  _disks;
	Ui::CPanelWidget              * ui;
	CController                   * _controller;
	QItemSelectionModel           * _selectionModel;
	CFileListModel                * _model;
	CFileListSortFilterProxyModel * _sortModel;
	Panel			                _panelPosition;
	bool                            _shiftPressed;

	QShortcut                       _calcDirSizeShortcut;
	QShortcut                       _selectCurrentItemShortcut;
};

#endif // CPANELWIDGET_H

#include "cfilelistmodel.h"
#include "shell/cshell.h"
#include "ccontroller.h"
#include "../../columns.h"

#include <assert.h>

CFileListModel::CFileListModel(QTreeView * treeView, QObject *parent) :
	QStandardItemModel(0, NumberOfColumns, parent),
	_controller(CController::get()),
	_tree(treeView),
	_panel(UnknownPanel)
{
}

// Sets the position (left or right) of a panel that this model represents
void CFileListModel::setPanelPosition(Panel p)
{
	assert(_panel == UnknownPanel); // Doesn't make sense to call this method more than once
	_panel = p;
}

QTreeView *CFileListModel::treeView() const
{
	return _tree;
}



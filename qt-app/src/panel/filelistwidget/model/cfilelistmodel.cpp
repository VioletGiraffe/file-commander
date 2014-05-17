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

QVariant CFileListModel::data( const QModelIndex & index, int role /*= Qt::DisplayRole*/ ) const
{
	if (role == Qt::ToolTipRole)
	{
		if (!index.isValid())
			return QString();
		return QString::fromStdWString(CShell::toolTip(_controller.itemByHash(_panel, itemHash(index)).absoluteFilePath().toStdWString()));
	}
	else if (role == Qt::EditRole)
	{
		return _controller.itemByHash(_panel, itemHash(index)).fileName();
	}
	else
		return QStandardItemModel::data(index, role);
}

bool CFileListModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
	if (role == Qt::EditRole)
	{
		const qulonglong hash = itemHash(index);
		emit itemEdited(hash, value.toString());
		return false;
	}
	else
		return QStandardItemModel::setData(index, value, role);
}

Qt::ItemFlags CFileListModel::flags(const QModelIndex & index) const
{
	Qt::ItemFlags flags = QStandardItemModel::flags(index);
	return data(index) != "[..]" ? flags | Qt::ItemIsEditable : flags;
}

qulonglong CFileListModel::itemHash(const QModelIndex & index) const
{
	QStandardItem * itm = item(index.row(), 0);
	assert(itm);
	bool ok = false;
	const qulonglong hash = itm->data(Qt::UserRole).toULongLong(&ok);
	assert(ok);
	return ok ? hash : 0;
}


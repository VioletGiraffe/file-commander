#include "cfilelistmodel.h"
#include "shell/cshell.h"
#include "ccontroller.h"
#include "../../../cmainwindow.h"
#include "../../columns.h"

DISABLE_COMPILER_WARNINGS
#include <QMimeData>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <set>

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

QVariant CFileListModel::data(const QModelIndex & index, int role /*= Qt::DisplayRole*/) const
{
	if (role == Qt::ToolTipRole)
	{
		if (!index.isValid())
			return QString();

		const CFileSystemObject item = _controller.itemByHash(_panel, itemHash(index));
		return QString(item.fullName() % "\n\n" % QString::fromStdWString(CShell::toolTip(item.fullAbsolutePath().toStdWString())));
	}
	else if (role == Qt::EditRole)
	{
		return _controller.itemByHash(_panel, itemHash(index)).fullName();
	}
	else if (role == FullNameRole)
	{
		return _controller.itemByHash(_panel, itemHash(index)).fullName();
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

Qt::ItemFlags CFileListModel::flags(const QModelIndex & idx) const
{
	Qt::ItemFlags flags = QStandardItemModel::flags(idx);
	if (!idx.isValid())
		return flags;

	if (data(index(idx.row(), 0)) != "[..]")
		flags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled;

	const qulonglong hash = itemHash(idx);
	const CFileSystemObject item = _controller.itemByHash(_panel, hash);
	if (item.exists() && item.isDir())
		flags |= Qt::ItemIsDropEnabled;

	return  flags;
}

bool CFileListModel::canDropMimeData(const QMimeData * data, Qt::DropAction /*action*/, int row, int /*column*/, const QModelIndex & /*parent*/) const
{
	if (!data->hasUrls())
		return false;

	const QModelIndex idx = index(row, 0);
	return !idx.isValid() || _controller.itemByHash(_panel, itemHash(index(row, 0))).isDir();
}

QStringList CFileListModel::mimeTypes() const
{
	return QStringList("text/uri-list");
}

bool CFileListModel::dropMimeData(const QMimeData * data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex & parent)
{
	if (action == Qt::IgnoreAction)
		return true;
	else if (!data->hasUrls())
		return false;

	CFileSystemObject dest = parent.isValid() ? _controller.itemByHash(_panel, itemHash(parent)) : CFileSystemObject(_controller.panel(_panel).currentDirPathNative());
	if (!dest.exists() || !dest.isDir())
	{
		assert(dest.exists() && dest.isDir());
		return false;
	}

	const QList<QUrl> urls(data->urls());
	std::vector<CFileSystemObject> objects;
	for(const QUrl& url: urls)
		objects.emplace_back(url.toLocalFile());

	if (objects.empty())
		return false;

	if (action == Qt::CopyAction)
		return CMainWindow::get()->copyFiles(objects, dest.fullAbsolutePath());
	else if (action == Qt::MoveAction)
		return CMainWindow::get()->moveFiles(objects, dest.fullAbsolutePath());
	else
		return false;
}

QMimeData *CFileListModel::mimeData(const QModelIndexList & indexes) const
{
	QMimeData * mime = new QMimeData();
	QList<QUrl> urls;
	std::set<int> rows;
	for(const auto& idx: indexes)
	{
		if (idx.isValid() && rows.count(idx.row()) == 0)
		{
			const QString path = _controller.itemByHash(_panel, itemHash(index(idx.row(), 0))).fullAbsolutePath();
			if (!path.isEmpty())
			{
				rows.insert(idx.row());
				urls.push_back(QUrl::fromLocalFile(path));
			}
		}
	}

	mime->setUrls(urls);
	return mime;
}

qulonglong CFileListModel::itemHash(const QModelIndex & index) const
{
	QStandardItem * itm = item(index.row(), 0);
	if (!itm)
		return 0;

	bool ok = false;
	const qulonglong hash = itm->data(Qt::UserRole).toULongLong(&ok);
	assert(ok);
	return ok ? hash : 0;
}


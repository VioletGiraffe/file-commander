#include "cfilelistmodel.h"
#include "shell/cshell.h"
#include "ccontroller.h"
#include "../../../cmainwindow.h"
#include "../../columns.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QMimeData>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <set>

CFileListModel::CFileListModel(QTreeView * treeView, QObject *parent) :
	QStandardItemModel(0, NumberOfColumns, parent),
	_controller(CController::get()),
	_tree(treeView)
{
}

// Sets the position (left or right) of a panel that this model represents
void CFileListModel::setPanelPosition(Panel p)
{
	assert_r(_panel == UnknownPanel); // Doesn't make sense to call this method more than once
	_panel = p;
}

Panel CFileListModel::panelPosition() const
{
	return _panel;
}

QTreeView *CFileListModel::treeView() const
{
	return _tree;
}

QVariant CFileListModel::data(const QModelIndex & index, int role) const
{
	if (role == Qt::ToolTipRole)
	{
		if (!index.isValid())
			return QString();

		const CFileSystemObject item = _controller.itemByHash(_panel, itemHash(index));
		return static_cast<QString>(item.fullName() % "\n\n" % QString::fromStdWString(OsShell::toolTip(item.fullAbsolutePath().toStdWString())));
	}
	else if (role == Qt::EditRole || role == FullNameRole)
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

Qt::ItemFlags CFileListModel::flags(const QModelIndex& index) const
{
	const Qt::ItemFlags flags = QStandardItemModel::flags(index);
	if (!index.isValid())
		return flags;

	const qulonglong hash = itemHash(index);
	const CFileSystemObject item = _controller.itemByHash(_panel, hash);

	if (!item.exists())
		return flags;
	else if (item.isCdUp())
		return flags & ~Qt::ItemIsSelectable;
	else
		return flags | Qt::ItemIsEditable;
}

bool CFileListModel::canDropMimeData(const QMimeData * data, Qt::DropAction /*action*/, int /*row*/, int /*column*/, const QModelIndex & /*parent*/) const
{
	return data->hasUrls();
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
	if (dest.isFile())
		dest = CFileSystemObject(dest.parentDirPath());
	assert_and_return_r(dest.exists() && dest.isDir(), false);

	const QList<QUrl> urls = data->urls();
	std::vector<CFileSystemObject> objects;
	for(const QUrl& url: urls)
		objects.emplace_back(url.toLocalFile());

	if (objects.empty())
		return false;

	if (action == Qt::CopyAction)
		return CMainWindow::get()->copyFiles(std::move(objects), dest.fullAbsolutePath());
	else if (action == Qt::MoveAction)
		return CMainWindow::get()->moveFiles(std::move(objects), dest.fullAbsolutePath());
	else
		return false;
}

QMimeData *CFileListModel::mimeData(const QModelIndexList & indexes) const
{
	auto* mime = new QMimeData();
	QList<QUrl> urls;
	std::set<int> rows;
	for(const auto& idx: indexes)
	{
		if (idx.isValid() && !rows.contains(idx.row()))
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
	assert_and_return_r(ok, 0);
	return hash;
}

#include "cfilelistmodel.h"
#include "shell/cshell.h"
#include "ccontroller.h"
#include "filesystemhelperfunctions.h"
#include "iconprovider/ciconprovider.h"
#include "../../../cmainwindow.h"
#include "../../columns.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QMimeData>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <unordered_set>

inline QVariant itemData(const CFileSystemObject& item, int column)
{
	const auto& props = item.properties();

	switch (column)
	{
	case NameColumn:
		if (props.type == Directory)
			return QString("[" % (item.isCdUp() ? QLatin1String("..") : props.fullName) % "]");
		else if (props.completeBaseName.isEmpty() && props.type == File) // File without a name, displaying extension in the name field and adding point to extension
			return item.extension().prepend('.');
		else
			return props.completeBaseName;

	case ExtColumn:
		if (!item.isCdUp() && !props.completeBaseName.isEmpty() && !props.extension.isEmpty())
			return props.extension;
		else
			return {};

	case SizeColumn:
		if (props.size > 0 || props.type == File)
			return fileSizeToString(props.size);
		else
			return {};

	case DateColumn:
		if (!item.isCdUp()) [[likely]]
			return item.modificationDateString();
		else
			return {};

	default:
		return {};
	}
}

CFileListModel::CFileListModel(Panel p, QObject *parent) :
	QAbstractItemModel(parent),
	_controller(CController::get()),
	_panel(p)
{
}

Panel CFileListModel::panelPosition() const
{
	return _panel;
}

void CFileListModel::onPanelContentsChanged(std::vector<qulonglong> itemHashes)
{
	emit beginResetModel();
	_itemHashes = std::move(itemHashes);
	emit endResetModel();
}

QModelIndex CFileListModel::index(int row, int column, const QModelIndex& parent) const
{
	if (!hasIndex(row, column, parent)) [[unlikely]] // is it?
		return {};

	return createIndex(row, column);
}

QModelIndex CFileListModel::parent(const QModelIndex& /*child*/) const
{
	return {}; // All items are top-level
}

int CFileListModel::rowCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) [[likely]]
		return (int)_itemHashes.size();
	else
		return 0; // All items are top-level
}

int CFileListModel::columnCount(const QModelIndex& parent) const
{
	if (!parent.isValid()) [[likely]]
		return NumberOfColumns;
	else
		return 0; // All items are top-level
}

QVariant CFileListModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid())
		return {};

	const CFileSystemObject& item = _controller.itemByHash(_panel, itemHash(index));

	switch (role)
	{
	case Qt::ToolTipRole:
		return static_cast<QString>(item.fullName() % "\n\n" % QString::fromStdWString(OsShell::toolTip(item.fullAbsolutePath().toStdWString())));
	case Qt::EditRole: [[fallthrough]];
	case FullNameRole:
		return item.fullName();
	case Qt::DisplayRole:
		return ::itemData(item, index.column());
	case Qt::DecorationRole:
		if (index.column() == NameColumn && !item.isCdUp())
			return CIconProvider::iconForFilesystemObject(item, false);
		else
			return {};
	default:
		return {}; // TODO: check this
	}
}

bool CFileListModel::setData(const QModelIndex & index, const QVariant & value, int role)
{
	if (role == Qt::EditRole)
	{
		const qulonglong hash = itemHash(index);
		emit itemEdited(hash, value.toString());
		return false;
	}
	
	return false;
}

Qt::ItemFlags CFileListModel::flags(const QModelIndex& index) const
{
	if (!index.isValid())
		return Qt::NoItemFlags;

	static constexpr Qt::ItemFlags flags =
		Qt::ItemIsSelectable
		| Qt::ItemIsEnabled
		| Qt::ItemIsEditable
		| Qt::ItemIsDragEnabled
		| Qt::ItemIsDropEnabled;

	const qulonglong hash = itemHash(index);
	const CFileSystemObject item = _controller.itemByHash(_panel, hash);

	if (!item.exists())
		return flags;
	else if (item.isCdUp())
		return flags & ~Qt::ItemIsSelectable;
	else
		return flags | Qt::ItemIsEditable;
}

QVariant CFileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};

	switch (section)
	{
	case 0:
		return tr("Name");
	case 1:
		return tr("Ext");
	case 2:
		return tr("Size");
	case 3:
		return tr("Date");
	default:
		assert_debug_only(false);
		return QVariant();
	}
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
	std::unordered_set<int> rows;
	for(const auto& idx: indexes)
	{
		if (idx.isValid() && !rows.contains(idx.row()))
		{
			const QString path = _controller.itemByHash(_panel, itemHash(idx.row())).fullAbsolutePath();
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

qulonglong CFileListModel::itemHash(int row) const
{
	return row < _itemHashes.size() ? _itemHashes[row] : 0;
}

qulonglong CFileListModel::itemHash(const QModelIndex & index) const
{
	assert_debug_only(index.isValid());
	return itemHash(index.row());
}

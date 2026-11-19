#include "cfilelistmodel.h"

#include "filelist/filesizetext.h"
#include "filelist/filetimetext.h"
#include "iconprovider/ciconprovider.h"
#include "iconprovider/fileglyph.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QIcon>
#include <QMimeData>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>
#include <variant>

static QVariant displayText(const CFileSystemObject& row, int column, const QString& listedFolder)
{
	switch (column)
	{
	case NameColumn:
		if (row.type() == Directory)
			return QString("[" % fileListFolderPrefix(row, listedFolder) % row.fullName() % "]");
		else
			return QString(fileListFolderPrefix(row, listedFolder) % fileListDisplayName(row));

	case ExtColumn:
		if (const QStringView extension = row.extension(); !extension.isEmpty())
			return extension.toString();
		else
			return {};

	case SizeColumn:
		if (row.size() > 0 || row.type() == File)
			return compactFileSizeText(row.size(), FileSizeUnitSpacing::Spaced);
		else
			return {};

	case DateColumn:
		if (!row.isCdUp()) [[likely]]
			return fileTimeText(row.modificationTime(), FileTimeTextLength::Short);
		else
			return {};

	default:
		return {};
	}
}

static QVariant textAlignment(int column)
{
	return column == SizeColumn ? QVariant::fromValue(Qt::Alignment{ Qt::AlignRight | Qt::AlignVCenter }) : QVariant{};
}

static QAbstractItemModel::LayoutChangeHint layoutChangeHint(SortedFileListReorderCause cause)
{
	return cause == SortedFileListReorderCause::Sort ? QAbstractItemModel::VerticalSortHint : QAbstractItemModel::NoLayoutChangeHint;
}

CFileListModel::CFileListModel(CIconProvider* iconProvider, QObject* parent) :
	QAbstractItemModel(parent),
	_iconProvider(iconProvider)
{
}

void CFileListModel::setDropHandler(DropHandler handler)
{
	_dropHandler = std::move(handler);
}

void CFileListModel::setRows(std::vector<CFileSystemObject> rows, QString listedFolder)
{
	_list.setRows(std::move(rows), std::move(listedFolder));
}

bool CFileListModel::updateRows(std::vector<CFileSystemObject> rows)
{
	return _list.updateRows(std::move(rows));
}

void CFileListModel::setNameFilter(const QString& filterText)
{
	_list.setNameFilter(filterText);
}

void CFileListModel::sort(int column, Qt::SortOrder order)
{
	assert_and_return_r(column < NumberOfColumns, );

	// A negative column is QTreeView asking for no sort column: the rows keep their order
	if (column >= 0)
		_list.setSorting(sortFieldOf(column), order);

	emit sorted();
}

int CFileListModel::sortColumn() const noexcept
{
	return columnOf(_list.sortField());
}

Qt::SortOrder CFileListModel::sortOrder() const noexcept
{
	return _list.sortOrder();
}

const CFileSystemObject& CFileListModel::rowAt(int row) const
{
	return _list.rowAt(row);
}

const CFileSystemObject& CFileListModel::rowAt(const QModelIndex& index) const
{
	assert_debug_only(index.isValid());
	return rowAt(index.row());
}

qulonglong CFileListModel::itemHash(const QModelIndex& index) const
{
	return index.isValid() ? rowAt(index.row()).hash() : 0;
}

QModelIndex CFileListModel::indexByHash(qulonglong hash) const
{
	const int row = _list.rowOf(hash);
	return row >= 0 ? createIndex(row, 0) : QModelIndex{};
}

int CFileListModel::firstFileRow() const
{
	return _list.firstFileRow();
}

const FolderContentsSummary& CFileListModel::contentsSummary() const noexcept
{
	return _list.contentsSummary();
}

void CFileListModel::onIconsChanged(const std::vector<qulonglong>& objectHashes)
{
	for (const qulonglong objectHash : objectHashes)
	{
		const QModelIndex itemIndex = indexByHash(objectHash);
		if (itemIndex.isValid())
			emit dataChanged(itemIndex, itemIndex, {Qt::DecorationRole, FileGlyphRole});
	}
}

void CFileListModel::onAllIconsChanged()
{
	if (_list.rowCount() == 0)
		return;

	emit dataChanged(index(0, NameColumn), index(_list.rowCount() - 1, NameColumn), {Qt::DecorationRole, FileGlyphRole});
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
		return _list.rowCount();
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

	const CFileSystemObject& row = rowAt(index.row());

	switch (role)
	{
	case Qt::EditRole: [[fallthrough]];
	case FullNameRole:
		return row.fullName().toString();
	case Qt::DisplayRole:
		return displayText(row, index.column(), _list.listedFolder());
	case Qt::TextAlignmentRole:
		return textAlignment(index.column());
	case Qt::DecorationRole:
	case FileGlyphRole:
	{
		if (!_iconProvider || index.column() != NameColumn || row.isCdUp())
			return {};

		const CIconProvider::FileIcon icon = _iconProvider->bestAvailableIconFor(row);
		if (role == Qt::DecorationRole)
			return _iconProvider->qIcon(icon);

		const FileGlyph* const glyph = std::get_if<FileGlyph>(&icon);
		return glyph ? QVariant{ static_cast<int>(*glyph) } : QVariant{};
	}
	default:
		return {};
	}
}

bool CFileListModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role == Qt::EditRole)
		emit itemEdited(itemHash(index), value.toString());

	return false;
}

Qt::ItemFlags CFileListModel::flags(const QModelIndex& index) const
{
	if (!index.isValid())
		return Qt::ItemIsDropEnabled;

	static constexpr Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsDropEnabled;
	if (rowAt(index.row()).isCdUp())
		return flags;
	else [[likely]]
		return flags | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
}

QVariant CFileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal)
		return {};
	if (role == Qt::TextAlignmentRole)
		return textAlignment(section);
	if (role != Qt::DisplayRole)
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
	const QList<QUrl> urls = data->urls();
	return std::any_of(urls.cbegin(), urls.cend(), [](const QUrl& url) { return url.isLocalFile(); });
}

QStringList CFileListModel::mimeTypes() const
{
	return QStringList("text/uri-list");
}

bool CFileListModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int /*row*/, int /*column*/, const QModelIndex& parent)
{
	if (action == Qt::IgnoreAction)
		return true;

	if (!_dropHandler)
		return false;

	return _dropHandler(data->urls(), action, parent.isValid() ? rowAt(parent.row()).fullAbsolutePath() : QString{});
}

QMimeData *CFileListModel::mimeData(const QModelIndexList & indexes) const
{
	auto* mime = new QMimeData();
	QList<QUrl> urls;
	ankerl::unordered_dense::set<int> rows;
	for(const auto& idx: indexes)
	{
		if (idx.isValid() && !rows.contains(idx.row()))
		{
			const QString& path = rowAt(idx.row()).fullAbsolutePath();
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

void CFileListModel::beforeReset()
{
	beginResetModel();
}

void CFileListModel::afterReset()
{
	endResetModel();
}

void CFileListModel::beforeInsert(int first, int last)
{
	beginInsertRows({}, first, last);
}

void CFileListModel::afterInsert()
{
	endInsertRows();
}

void CFileListModel::beforeRemove(int first, int last)
{
	beginRemoveRows({}, first, last);
}

void CFileListModel::afterRemove()
{
	endRemoveRows();
}

void CFileListModel::beforeMove(int row, int destination)
{
	beginMoveRows({}, row, row, {}, destination);
}

void CFileListModel::afterMove()
{
	endMoveRows();
}

void CFileListModel::rowChanged(int row)
{
	emit dataChanged(index(row, 0), index(row, NumberOfColumns - 1));
}

void CFileListModel::beforeReorder(SortedFileListReorderCause cause)
{
	emit layoutAboutToBeChanged({}, layoutChangeHint(cause));

	_indexesBeforeReorder = persistentIndexList();
	_hashesBeforeReorder.clear();
	_hashesBeforeReorder.reserve((size_t)_indexesBeforeReorder.size());
	for (const QModelIndex& oldIndex : _indexesBeforeReorder)
		_hashesBeforeReorder.push_back(itemHash(oldIndex));
}

void CFileListModel::afterReorder(SortedFileListReorderCause cause)
{
	QModelIndexList newIndexes;
	newIndexes.reserve(_indexesBeforeReorder.size());
	for (qsizetype i = 0; i < _indexesBeforeReorder.size(); ++i)
	{
		const QModelIndex newRow = indexByHash(_hashesBeforeReorder[(size_t)i]);
		newIndexes.push_back(newRow.isValid() ? createIndex(newRow.row(), _indexesBeforeReorder[i].column()) : QModelIndex{});
	}

	changePersistentIndexList(_indexesBeforeReorder, newIndexes);
	_indexesBeforeReorder.clear();
	_hashesBeforeReorder.clear();
	emit layoutChanged({}, layoutChangeHint(cause));
}


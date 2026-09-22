#include "cfilelistmodel.h"

#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QMimeData>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

FileListRow FileListRow::fromObject(const CFileSystemObject& object)
{
	const CFileSystemObjectProperties& properties = object.properties();

	FileListRow row;
	row.fullPath = properties.fullPath;
	row.fullName = properties.fullName;
	row.name = properties.completeBaseName;
	row.extension = properties.extension;
	row.hash = properties.hash;
	row.size = properties.size;
	row.modificationTime = properties.modificationTime;
	row.type = properties.type;
	row.isCdUp = object.isCdUp();
	return row;
}

const QString& FileListRow::displayName() const noexcept
{
	return name.isEmpty() && isFileOrBundle() ? fullName : name;
}

const QString& FileListRow::displayExtension() const noexcept
{
	static const QString none;
	return name.isEmpty() ? none : extension;
}

static QVariant displayText(const FileListRow& row, int column)
{
	switch (column)
	{
	case NameColumn:
		if (row.type == Directory)
			return QString("[" % (row.isCdUp ? QLatin1String("..") : row.fullName) % "]");
		else
			return row.displayName();

	case ExtColumn:
		if (!row.isCdUp && !row.displayExtension().isEmpty())
			return row.displayExtension();
		else
			return {};

	case SizeColumn:
		if (row.size > 0 || row.type == File)
			return fileSizeToString(row.size);
		else
			return {};

	case DateColumn:
		if (!row.isCdUp) [[likely]]
			return fromTime_t(row.modificationTime).toString("dd.MM.yyyy hh:mm:ss");
		else
			return {};

	default:
		return {};
	}
}

template <typename T>
[[nodiscard]] static int compareValues(const T& l, const T& r) noexcept
{
	return l < r ? -1 : (r < l ? 1 : 0);
}

// Ascending order by the column alone
static int compareByColumn(const FileListRow& l, const FileListRow& r, int column)
{
	switch (column)
	{
	case NameColumn:
		return NaturalSort::compare(l.displayName(), r.displayName());
	case ExtColumn:
		// Folders by name, files by extension, then name
		if (l.isFileOrBundle())
		{
			if (const int byExtension = NaturalSort::compare(l.displayExtension(), r.displayExtension()); byExtension != 0)
				return byExtension;
		}
		return NaturalSort::compare(l.displayName(), r.displayName());
	case SizeColumn:
		return compareValues(l.size, r.size);
	case DateColumn:
		return compareValues(l.modificationTime, r.modificationTime);
	default:
		assert_unconditional_r("Unhandled sort column");
		return 0;
	}
}

CFileListModel::CFileListModel(CIconProvider* iconProvider, QObject* parent) :
	QAbstractItemModel(parent),
	_iconProvider(iconProvider)
{
	_nameFilter.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
}

void CFileListModel::setDropHandler(DropHandler handler)
{
	_dropHandler = std::move(handler);
}

void CFileListModel::setRows(std::vector<FileListRow> rows)
{
	beginResetModel();

	_rows = std::move(rows);

	_contentsSummary = {};
	for (const FileListRow& row : _rows)
	{
		if (row.isCdUp)
			continue;

		if (row.type == File)
			++_contentsSummary.numFiles;
		else if (row.isDir())
			++_contentsSummary.numFolders;

		_contentsSummary.size += row.size;
	}

	_displayedRows = displayedRowsInOrder();
	updateRowByHash();

	endResetModel();
}

void CFileListModel::setNameFilter(const QString& wildcard)
{
	const QString pattern = wildcard.isEmpty() ? QString{} : QRegularExpression::wildcardToRegularExpression(wildcard, QRegularExpression::UnanchoredWildcardConversion);
	if (pattern == _nameFilter.pattern())
		return;

	_nameFilter.setPattern(pattern);
	relayout(QAbstractItemModel::NoLayoutChangeHint);
}

void CFileListModel::sort(int column, Qt::SortOrder order)
{
	assert_and_return_r(column < NumberOfColumns, );

	// A negative column is QTreeView asking for no sort column: the rows keep their order
	if (column >= 0 && (column != _sortColumn || order != _sortOrder))
	{
		_sortColumn = column;
		_sortOrder = order;
		relayout(QAbstractItemModel::VerticalSortHint);
	}

	emit sorted();
}

int CFileListModel::sortColumn() const noexcept
{
	return _sortColumn;
}

Qt::SortOrder CFileListModel::sortOrder() const noexcept
{
	return _sortOrder;
}

const FileListRow& CFileListModel::rowAt(int row) const
{
	assert_debug_only(row >= 0 && row < (int)_displayedRows.size());
	return _rows[_displayedRows[(size_t)row]];
}

const FileListRow& CFileListModel::rowAt(const QModelIndex& index) const
{
	assert_debug_only(index.isValid());
	return rowAt(index.row());
}

qulonglong CFileListModel::itemHash(const QModelIndex& index) const
{
	return index.isValid() ? rowAt(index.row()).hash : 0;
}

QModelIndex CFileListModel::indexByHash(qulonglong hash) const
{
	const auto row = _displayRowByHash.find(hash);
	return row != _displayRowByHash.end() ? createIndex(row->second, 0) : QModelIndex{};
}

int CFileListModel::firstFileRow() const
{
	const auto firstFile = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [this](uint32_t rowIndex) {
		return !_rows[rowIndex].isFileOrBundle();
	});

	return firstFile != _displayedRows.cend() ? (int)(firstFile - _displayedRows.cbegin()) : -1;
}

const FolderContentsSummary& CFileListModel::contentsSummary() const noexcept
{
	return _contentsSummary;
}

void CFileListModel::onPreciseIconsAvailable(const std::vector<qulonglong>& objectHashes)
{
	for (const qulonglong objectHash : objectHashes)
	{
		const QModelIndex itemIndex = indexByHash(objectHash);
		if (itemIndex.isValid())
			emit dataChanged(itemIndex, itemIndex, {Qt::DecorationRole});
	}
}

void CFileListModel::onAllIconsInvalidated()
{
	if (_displayedRows.empty())
		return;

	emit dataChanged(index(0, NameColumn), index((int)_displayedRows.size() - 1, NameColumn), {Qt::DecorationRole});
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
		return (int)_displayedRows.size();
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

	const FileListRow& row = rowAt(index.row());

	switch (role)
	{
	case Qt::EditRole: [[fallthrough]];
	case FullNameRole:
		return row.fullName;
	case Qt::DisplayRole:
		return displayText(row, index.column());
	case Qt::DecorationRole:
		if (_iconProvider && index.column() == NameColumn && !row.isCdUp)
			return _iconProvider->bestAvailableIconFor(row.extension, row.isDir(), row.fullPath, row.modificationTime);
		else
			return {};
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
	if (rowAt(index.row()).isCdUp)
		return flags;
	else [[likely]]
		return flags | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
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

	return _dropHandler(data->urls(), action, parent.isValid() ? rowAt(parent.row()).fullPath : QString{});
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
			const QString& path = rowAt(idx.row()).fullPath;
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

bool CFileListModel::rowLessThan(const FileListRow& l, const FileListRow& r) const
{
	// [..] first, then folders, then files, in either direction
	if (l.isCdUp != r.isCdUp)
		return l.isCdUp;
	if (l.isFileOrBundle() != r.isFileOrBundle())
		return r.isFileOrBundle();

	int result = compareByColumn(l, r, _sortColumn);
	if (result == 0)
		result = NaturalSort::compare(l.fullPath, r.fullPath); // Unique, so the order is total

	return _sortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
}

std::vector<uint32_t> CFileListModel::displayedRowsInOrder() const
{
	std::vector<uint32_t> displayedRows;
	displayedRows.reserve(_rows.size());
	const bool filtered = !_nameFilter.pattern().isEmpty();
	for (uint32_t i = 0, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
	{
		if (!filtered || _rows[i].fullName.contains(_nameFilter))
			displayedRows.push_back(i);
	}

	std::sort(displayedRows.begin(), displayedRows.end(), [this](uint32_t l, uint32_t r) {
		return rowLessThan(_rows[l], _rows[r]);
	});

	return displayedRows;
}

void CFileListModel::updateRowByHash()
{
	_displayRowByHash.clear();
	_displayRowByHash.reserve(_displayedRows.size());
	for (int row = 0, numRows = (int)_displayedRows.size(); row < numRows; ++row)
		_displayRowByHash.emplace(_rows[_displayedRows[(size_t)row]].hash, row);
}

void CFileListModel::relayout(QAbstractItemModel::LayoutChangeHint hint)
{
	emit layoutAboutToBeChanged({}, hint);

	const QModelIndexList oldIndexes = persistentIndexList();
	std::vector<qulonglong> oldHashes;
	oldHashes.reserve((size_t)oldIndexes.size());
	for (const QModelIndex& oldIndex : oldIndexes)
		oldHashes.push_back(itemHash(oldIndex));

	_displayedRows = displayedRowsInOrder();
	updateRowByHash();

	QModelIndexList newIndexes;
	newIndexes.reserve(oldIndexes.size());
	for (qsizetype i = 0; i < oldIndexes.size(); ++i)
	{
		const QModelIndex newRow = indexByHash(oldHashes[(size_t)i]);
		newIndexes.push_back(newRow.isValid() ? createIndex(newRow.row(), oldIndexes[i].column()) : QModelIndex{});
	}

	changePersistentIndexList(oldIndexes, newIndexes);
	emit layoutChanged({}, hint);
}

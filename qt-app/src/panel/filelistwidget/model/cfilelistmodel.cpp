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
#include <functional>
#include <utility>

[[nodiscard]] static bool isFileOrBundle(const CFileSystemObject& row) noexcept
{
	return row.isFile() || row.isBundle();
}

// A file with nothing before its extension, like .jpg on Windows, displays and sorts by its full name
[[nodiscard]] static QStringView displayName(const CFileSystemObject& row) noexcept
{
	const QStringView name = row.name();
	return name.isEmpty() && isFileOrBundle(row) ? row.fullName() : name;
}

static QVariant displayText(const CFileSystemObject& row, int column)
{
	switch (column)
	{
	case NameColumn:
		if (row.type() == Directory)
			return QString("[" % row.fullName() % "]");
		else
			return displayName(row).toString();

	case ExtColumn:
		if (const QStringView extension = row.extension(); !extension.isEmpty())
			return extension.toString();
		else
			return {};

	case SizeColumn:
		if (row.size() > 0 || row.type() == File)
			return fileSizeToString(row.size());
		else
			return {};

	case DateColumn:
		if (!row.isCdUp()) [[likely]]
			return fromTime_t(row.modificationTime()).toString("dd.MM.yyyy hh:mm:ss");
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

[[nodiscard]] static QCollatorSortKey nameSortKey(const CFileSystemObject& row)
{
	return NaturalSort::sortKey(displayName(row).toString());
}


// updateRows() resets once the displayed rows it would change exceed both
// A third: from there on, one reset of 100 000 rows is cheaper, per the timing case in the filelist suite
static constexpr size_t MinChangesForReset = 1000;
static constexpr size_t ReciprocalRowShareForReset = 3;

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

void CFileListModel::setRows(std::vector<CFileSystemObject> rows)
{
	beginResetModel();

	_rows = std::move(rows);
	_nameSortKeys.clear();
	_extensionSortKeys.clear();
	_sortKeyByExtension.clear();
	_nameSortKeys.reserve(_rows.size());
	_extensionSortKeys.reserve(_rows.size());
	for (const CFileSystemObject& row : _rows)
		appendSortKeys(row);

	updateContentsSummary();
	_displayedRows = displayedRowsInOrder();
	_displayRowByHashIsStale = true;

	endResetModel();
}

bool CFileListModel::updateRows(std::vector<CFileSystemObject> rows)
{
	const auto oldRowCount = (uint32_t)_rows.size();

	ankerl::unordered_dense::map<qulonglong, uint32_t, IdentityHash> oldRowByHash;
	oldRowByHash.reserve(oldRowCount);
	for (uint32_t i = 0; i < oldRowCount; ++i)
		oldRowByHash.emplace(_rows[i].hash(), i);

	std::vector<int> displayRowOfOldRow(oldRowCount, -1);
	for (int displayRow = 0, numDisplayed = (int)_displayedRows.size(); displayRow < numDisplayed; ++displayRow)
		displayRowOfOldRow[_displayedRows[(size_t)displayRow]] = displayRow;

	// Indices into rows
	std::vector<uint32_t> addedRows;
	// Old row index, new row index
	std::vector<std::pair<uint32_t, uint32_t>> changedRows;
	std::vector<bool> oldRowKept(oldRowCount);
	size_t numDisplayedChanges = 0;
	for (uint32_t i = 0, numRows = (uint32_t)rows.size(); i < numRows; ++i)
	{
		const CFileSystemObject& row = rows[i];
		const auto old = oldRowByHash.find(row.hash());
		// A hash collision is a different item
		if (old == oldRowByHash.end() || _rows[old->second].fullAbsolutePath() != row.fullAbsolutePath() || _rows[old->second].isCdUp() != row.isCdUp())
		{
			addedRows.push_back(i);
			numDisplayedChanges += passesNameFilter(row) ? 1 : 0;
			continue;
		}

		oldRowKept[old->second] = true;
		const CFileSystemObject& oldRow = _rows[old->second];
		if (oldRow.size() != row.size() || oldRow.modificationTime() != row.modificationTime() || oldRow.type() != row.type())
		{
			changedRows.emplace_back(old->second, i);
			numDisplayedChanges += displayRowOfOldRow[old->second] >= 0 ? 1 : 0;
		}
	}

	for (uint32_t i = 0; i < oldRowCount; ++i)
		numDisplayedChanges += !oldRowKept[i] && displayRowOfOldRow[i] >= 0 ? 1 : 0;

	if (numDisplayedChanges > std::max(MinChangesForReset, _displayedRows.size() / ReciprocalRowShareForReset))
	{
		setRows(std::move(rows));
		return false;
	}

	for (const auto& [oldRowIndex, newRowIndex] : changedRows)
	{
		// A type change can change the name and extension the row sorts by
		_rows[oldRowIndex] = std::move(rows[newRowIndex]);
		_nameSortKeys[oldRowIndex] = nameSortKey(_rows[oldRowIndex]);
		_extensionSortKeys[oldRowIndex] = extensionSortKey(_rows[oldRowIndex]);

		int displayRow = displayRowOfOldRow[oldRowIndex];
		if (displayRow < 0)
			continue;

		// A move, unlike a removal and an insertion, keeps the row's selection and open editor
		const int destination = moveDestination(oldRowIndex, displayRow);
		if (destination != displayRow && destination != displayRow + 1)
		{
			const auto first = _displayedRows.begin();
			beginMoveRows({}, displayRow, displayRow, {}, destination);
			if (destination < displayRow)
				std::rotate(first + destination, first + displayRow, first + displayRow + 1);
			else
				std::rotate(first + displayRow, first + displayRow + 1, first + destination);

			for (int row = std::min(displayRow, destination), end = std::max(displayRow + 1, destination); row < end; ++row)
				displayRowOfOldRow[_displayedRows[(size_t)row]] = row;

			_displayRowByHashIsStale = true;
			endMoveRows();
			displayRow = displayRowOfOldRow[oldRowIndex];
		}

		emit dataChanged(index(displayRow, 0), index(displayRow, NumberOfColumns - 1));
	}

	std::vector<int> removedDisplayRows;
	for (uint32_t i = 0; i < oldRowCount; ++i)
	{
		if (!oldRowKept[i] && displayRowOfOldRow[i] >= 0)
			removedDisplayRows.push_back(displayRowOfOldRow[i]);
	}

	// Bottom up, so the rows of the runs still to remove keep their numbers
	std::sort(removedDisplayRows.begin(), removedDisplayRows.end(), std::greater{});
	for (size_t runBegin = 0, numRemoved = removedDisplayRows.size(); runBegin < numRemoved;)
	{
		size_t runEnd = runBegin + 1;
		while (runEnd < numRemoved && removedDisplayRows[runEnd] == removedDisplayRows[runEnd - 1] - 1)
			++runEnd;

		const int firstRow = removedDisplayRows[runEnd - 1], lastRow = removedDisplayRows[runBegin];
		beginRemoveRows({}, firstRow, lastRow);
		_displayedRows.erase(_displayedRows.begin() + firstRow, _displayedRows.begin() + lastRow + 1);
		_displayRowByHashIsStale = true;
		endRemoveRows();

		runBegin = runEnd;
	}

	std::vector<uint32_t> insertedRows;
	for (const uint32_t addedRow : addedRows)
	{
		if (passesNameFilter(rows[addedRow]))
			insertedRows.push_back((uint32_t)_rows.size());

		appendSortKeys(rows[addedRow]);
		_rows.push_back(std::move(rows[addedRow]));
	}

	std::sort(insertedRows.begin(), insertedRows.end(), [this](uint32_t l, uint32_t r) {
		return rowLessThan(l, r);
	});

	for (size_t runBegin = 0, numInserted = insertedRows.size(); runBegin < numInserted;)
	{
		const uint32_t firstInRun = insertedRows[runBegin];
		const auto position = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [&](uint32_t rowIndex) {
			return rowLessThan(rowIndex, firstInRun);
		});

		// The run takes in every next row that also sorts above the row at position
		size_t runEnd = runBegin + 1;
		while (runEnd < numInserted && (position == _displayedRows.cend() || rowLessThan(insertedRows[runEnd], *position)))
			++runEnd;

		const auto firstRow = (int)(position - _displayedRows.cbegin());
		beginInsertRows({}, firstRow, firstRow + (int)(runEnd - runBegin) - 1);
		_displayedRows.insert(position, insertedRows.cbegin() + (ptrdiff_t)runBegin, insertedRows.cbegin() + (ptrdiff_t)runEnd);
		_displayRowByHashIsStale = true;
		endInsertRows();

		runBegin = runEnd;
	}

	// Drops the removed rows from _rows; indices, not display rows, change
	std::vector<uint32_t> compactedIndex(_rows.size());
	uint32_t numKept = 0;
	for (uint32_t i = 0, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
	{
		if (i < oldRowCount && !oldRowKept[i])
			continue;

		compactedIndex[i] = numKept;
		if (numKept != i)
		{
			_rows[numKept] = std::move(_rows[i]);
			_nameSortKeys[numKept] = std::move(_nameSortKeys[i]);
			_extensionSortKeys[numKept] = std::move(_extensionSortKeys[i]);
		}
		++numKept;
	}

	_rows.resize(numKept);
	// Not resize(): the keys have no default constructor
	_nameSortKeys.erase(_nameSortKeys.begin() + numKept, _nameSortKeys.end());
	_extensionSortKeys.erase(_extensionSortKeys.begin() + numKept, _extensionSortKeys.end());
	for (uint32_t& rowIndex : _displayedRows)
		rowIndex = compactedIndex[rowIndex];

	updateContentsSummary();
	return true;
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

const CFileSystemObject& CFileListModel::rowAt(int row) const
{
	assert_debug_only(row >= 0 && row < (int)_displayedRows.size());
	return _rows[_displayedRows[(size_t)row]];
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
	if (_displayRowByHashIsStale)
		rebuildDisplayRowByHash();

	const auto row = _displayRowByHash.find(hash);
	return row != _displayRowByHash.end() ? createIndex(row->second, 0) : QModelIndex{};
}

int CFileListModel::firstFileRow() const
{
	const auto firstFile = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [this](uint32_t rowIndex) {
		return !isFileOrBundle(_rows[rowIndex]);
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

	const CFileSystemObject& row = rowAt(index.row());

	switch (role)
	{
	case Qt::EditRole: [[fallthrough]];
	case FullNameRole:
		return row.fullName().toString();
	case Qt::DisplayRole:
		return displayText(row, index.column());
	case Qt::DecorationRole:
		if (_iconProvider && index.column() == NameColumn && !row.isCdUp())
			return _iconProvider->bestAvailableIconFor(row);
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
	if (rowAt(index.row()).isCdUp())
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

// Ascending order by the sort column alone
int CFileListModel::compareByColumn(uint32_t l, uint32_t r) const
{
	switch (_sortColumn)
	{
	case NameColumn:
		return NaturalSort::compare(_nameSortKeys[l], _nameSortKeys[r]);
	case ExtColumn:
		// Folders by name, files by extension, then name
		if (isFileOrBundle(_rows[l]))
		{
			if (const int byExtension = NaturalSort::compare(_extensionSortKeys[l], _extensionSortKeys[r]); byExtension != 0)
				return byExtension;
		}
		return NaturalSort::compare(_nameSortKeys[l], _nameSortKeys[r]);
	case SizeColumn:
		return compareValues(_rows[l].size(), _rows[r].size());
	case DateColumn:
		return compareValues(_rows[l].modificationTime(), _rows[r].modificationTime());
	default:
		assert_unconditional_r("Unhandled sort column");
		return 0;
	}
}

bool CFileListModel::rowLessThan(uint32_t l, uint32_t r) const
{
	const CFileSystemObject& left = _rows[l];
	const CFileSystemObject& right = _rows[r];

	// [..] first, then folders, then files, in either direction
	if (left.isCdUp() != right.isCdUp())
		return left.isCdUp();
	if (isFileOrBundle(left) != isFileOrBundle(right))
		return isFileOrBundle(right);

	// Ties go by name and extension, then by the exact path: unique, so the order is total.
	// Not the collator for the path: it can call two different strings equal.
	int result = compareByColumn(l, r);
	if (result == 0)
		result = NaturalSort::compare(_nameSortKeys[l], _nameSortKeys[r]);
	if (result == 0)
		result = NaturalSort::compare(_extensionSortKeys[l], _extensionSortKeys[r]);
	if (result == 0)
		result = left.fullAbsolutePath().compare(right.fullAbsolutePath());

	return _sortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
}

bool CFileListModel::passesNameFilter(const CFileSystemObject& row) const
{
	return _nameFilter.pattern().isEmpty() || row.fullName().contains(_nameFilter);
}

std::vector<uint32_t> CFileListModel::displayedRowsInOrder() const
{
	std::vector<uint32_t> displayedRows;
	displayedRows.reserve(_rows.size());
	for (uint32_t i = 0, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
	{
		if (passesNameFilter(_rows[i]))
			displayedRows.push_back(i);
	}

	std::sort(displayedRows.begin(), displayedRows.end(), [this](uint32_t l, uint32_t r) {
		return rowLessThan(l, r);
	});

	return displayedRows;
}

int CFileListModel::moveDestination(uint32_t rowIndex, int displayRow) const
{
	const auto sortsAboveRow = [&](uint32_t otherRowIndex) { return rowLessThan(otherRowIndex, rowIndex); };
	const auto first = _displayedRows.cbegin(), moved = first + displayRow;
	if (const auto above = std::partition_point(first, moved, sortsAboveRow); above != moved)
		return (int)(above - first);

	return (int)(std::partition_point(moved + 1, _displayedRows.cend(), sortsAboveRow) - first);
}

QCollatorSortKey CFileListModel::extensionSortKey(const CFileSystemObject& row)
{
	const QStringView extension = row.extension();
	if (const auto cached = _sortKeyByExtension.find(extension); cached != _sortKeyByExtension.end())
		return cached->second;

	QString extensionString = extension.toString();
	QCollatorSortKey key = NaturalSort::sortKey(extensionString);
	return _sortKeyByExtension.emplace(std::move(extensionString), std::move(key)).first->second;
}

void CFileListModel::appendSortKeys(const CFileSystemObject& row)
{
	_nameSortKeys.push_back(nameSortKey(row));
	_extensionSortKeys.push_back(extensionSortKey(row));
}

void CFileListModel::rebuildDisplayRowByHash() const
{
	_displayRowByHash.clear();
	_displayRowByHash.reserve(_displayedRows.size());
	for (int row = 0, numRows = (int)_displayedRows.size(); row < numRows; ++row)
		_displayRowByHash.emplace(_rows[_displayedRows[(size_t)row]].hash(), row);

	_displayRowByHashIsStale = false;
}

void CFileListModel::updateContentsSummary()
{
	_contentsSummary = {};
	for (const CFileSystemObject& row : _rows)
	{
		if (row.isCdUp())
			continue;

		if (row.type() == File)
			++_contentsSummary.numFiles;
		else if (row.isDir())
			++_contentsSummary.numFolders;

		_contentsSummary.size += row.size();
	}
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
	_displayRowByHashIsStale = true;

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

#include "sortedfilelist.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <functional>
#include <utility>

SortedFileList::SortedFileList(SortedFileListObserver& observer, const InitialOrder initialOrder) :
	_observer(observer),
	_inArrivalOrder(initialOrder == InitialOrder::Arrival)
{
}

// The fields an update compares: an item whose new data differs in them is a changed row
static bool itemDataDiffers(const CFileSystemObject& l, const CFileSystemObject& r)
{
	return l.size() != r.size() || l.modificationTime() != r.modificationTime() || l.type() != r.type();
}

void SortedFileList::setRows(std::vector<CFileSystemObject> rows, QString listedFolder)
{
	_observer.beforeReset();

	_rows = std::move(rows);
	rebuildRowIndexByHash();
	_sortKeys.clear();
	_sorter.clearExtensionKeyCache();
	_sorter.setListedFolder(std::move(listedFolder));
	_sortKeys.reserve(_rows.size());
	for (const CFileSystemObject& row : _rows)
		_sortKeys.push_back(_sorter.sortKeysFor(row));

	updateContentsSummary();
	_displayedRows = displayedRowsInOrder();

	_observer.afterReset();
}

bool SortedFileList::updateRows(std::vector<CFileSystemObject> rows)
{
	const auto oldRowCount = (uint32_t)_rows.size();

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
		const auto old = _rowIndexByHash.find(row.hash());
		// A hash collision is a different item
		if (old == _rowIndexByHash.end() || _rows[old->second].fullAbsolutePath() != row.fullAbsolutePath() || _rows[old->second].isCdUp() != row.isCdUp())
		{
			addedRows.push_back(i);
			numDisplayedChanges += _nameFilter.matches(row.fullName()) ? 1 : 0;
			continue;
		}

		oldRowKept[old->second] = true;
		if (itemDataDiffers(_rows[old->second], row))
		{
			changedRows.emplace_back(old->second, i);
			numDisplayedChanges += displayRowOfOldRow[old->second] >= 0 ? 1 : 0;
		}
	}

	for (uint32_t i = 0; i < oldRowCount; ++i)
		numDisplayedChanges += !oldRowKept[i] && displayRowOfOldRow[i] >= 0 ? 1 : 0;

	if (numDisplayedChanges > MaxChangesForUpdate)
	{
		setRows(std::move(rows), _sorter.listedFolder());
		return false;
	}

	for (const auto& [oldRowIndex, newRowIndex] : changedRows)
	{
		// A type change can change the name and extension the row sorts by
		_rows[oldRowIndex] = std::move(rows[newRowIndex]);
		_sortKeys[oldRowIndex] = _sorter.sortKeysFor(_rows[oldRowIndex]);

		const int displayRow = displayRowOfOldRow[oldRowIndex];
		if (displayRow < 0)
			continue;

		const int newDisplayRow = moveRowToSortedPosition(displayRow);
		for (int row = std::min(displayRow, newDisplayRow), last = std::max(displayRow, newDisplayRow); row <= last; ++row)
			displayRowOfOldRow[_displayedRows[(size_t)row]] = row;

		_observer.rowChanged(newDisplayRow);
	}

	std::vector<int> removedDisplayRows;
	for (uint32_t i = 0; i < oldRowCount; ++i)
	{
		if (!oldRowKept[i] && displayRowOfOldRow[i] >= 0)
			removedDisplayRows.push_back(displayRowOfOldRow[i]);
	}
	removeDisplayedRows(std::move(removedDisplayRows));

	std::vector<uint32_t> insertedRows;
	for (const uint32_t addedRow : addedRows)
	{
		if (_nameFilter.matches(rows[addedRow].fullName()))
			insertedRows.push_back((uint32_t)_rows.size());

		_sortKeys.push_back(_sorter.sortKeysFor(rows[addedRow]));
		_rows.push_back(std::move(rows[addedRow]));
	}
	insertDisplayedRows(std::move(insertedRows));

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
			_sortKeys[numKept] = std::move(_sortKeys[i]);
		}
		++numKept;
	}

	const bool rowsRemoved = numKept < _rows.size();
	_rows.resize(numKept);
	// Not resize(): the keys have no default constructor
	_sortKeys.erase(_sortKeys.begin() + numKept, _sortKeys.end());
	for (uint32_t& rowIndex : _displayedRows)
		rowIndex = compactedIndex[rowIndex];

	if (rowsRemoved)
		rebuildRowIndexByHash();
	else
	{
		for (uint32_t i = oldRowCount, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
			_rowIndexByHash.emplace(_rows[i].hash(), i);
	}

	updateContentsSummary();
	return true;
}

void SortedFileList::applyChanges(std::vector<CFileSystemObject> upserted, const std::vector<qulonglong>& removedHashes)
{
	assert_and_return_r(!_inArrivalOrder, );

	std::vector<uint32_t> removedRows;
	for (const qulonglong hash : removedHashes)
	{
		if (const auto rowIndex = _rowIndexByHash.find(hash); rowIndex != _rowIndexByHash.end())
			removedRows.push_back(rowIndex->second);
	}

	std::vector<CFileSystemObject> addedItems;
	for (CFileSystemObject& item : upserted)
	{
		const auto rowIndex = _rowIndexByHash.find(item.hash());
		if (rowIndex != _rowIndexByHash.end() && _rows[rowIndex->second].fullAbsolutePath() == item.fullAbsolutePath())
		{
			changeRow(rowIndex->second, std::move(item));
			continue;
		}

		// A hash collision is a different item
		if (rowIndex != _rowIndexByHash.end())
			removedRows.push_back(rowIndex->second);
		addedItems.push_back(std::move(item));
	}

	removeRows(std::move(removedRows));

	std::vector<uint32_t> insertedRows;
	for (CFileSystemObject& item : addedItems)
	{
		const auto rowIndex = (uint32_t)_rows.size();
		if (_nameFilter.matches(item.fullName()))
			insertedRows.push_back(rowIndex);

		_contentsSummary.add(item);
		_sortKeys.push_back(_sorter.sortKeysFor(item));
		_rowIndexByHash.emplace(item.hash(), rowIndex);
		_rows.push_back(std::move(item));
	}
	insertDisplayedRows(std::move(insertedRows));
}

void SortedFileList::addRows(std::vector<CFileSystemObject> rows)
{
	std::vector<uint32_t> displayedNewRows;
	for (CFileSystemObject& row : rows)
	{
		if (_nameFilter.matches(row.fullName()))
			displayedNewRows.push_back((uint32_t)_rows.size());

		_contentsSummary.add(row);
		_sortKeys.push_back(_sorter.sortKeysFor(row));
		_rowIndexByHash.emplace(row.hash(), (uint32_t)_rows.size());
		_rows.push_back(std::move(row));
	}

	if (displayedNewRows.empty())
		return;

	if (_inArrivalOrder)
	{
		const auto firstRow = (int)_displayedRows.size();
		_observer.beforeInsert(firstRow, firstRow + (int)displayedNewRows.size() - 1);
		_displayedRows.insert(_displayedRows.end(), displayedNewRows.cbegin(), displayedNewRows.cend());
		_observer.afterInsert();
		return;
	}

	const auto lessThan = [this](uint32_t l, uint32_t r) { return rowLessThan(l, r); };
	std::sort(displayedNewRows.begin(), displayedNewRows.end(), lessThan);

	_observer.beforeReorder(SortedFileListReorderCause::Addition);
	const auto oldRowCount = (ptrdiff_t)_displayedRows.size();
	_displayedRows.insert(_displayedRows.end(), displayedNewRows.cbegin(), displayedNewRows.cend());
	std::inplace_merge(_displayedRows.begin(), _displayedRows.begin() + oldRowCount, _displayedRows.end(), lessThan);
	_observer.afterReorder(SortedFileListReorderCause::Addition);
}

void SortedFileList::setNameFilter(const QString& filterText)
{
	if (filterText == _nameFilterText)
		return;

	_nameFilterText = filterText;
	_nameFilter = FileNameFilter{ splitNameFilterField(filterText), Qt::CaseInsensitive };
	refilterAndResort(SortedFileListReorderCause::Filter);
}

void SortedFileList::setSorting(FileListSortField field, Qt::SortOrder order)
{
	if (!_inArrivalOrder && field == _sorter.sortField() && order == _sorter.sortOrder())
		return;

	_inArrivalOrder = false;
	_sorter.setSorting(field, order);
	refilterAndResort(SortedFileListReorderCause::Sort);
}

void SortedFileList::showInArrivalOrder()
{
	if (_inArrivalOrder)
		return;

	_inArrivalOrder = true;
	_sorter.setSorting(FileListSortField::Name, Qt::AscendingOrder);
	refilterAndResort(SortedFileListReorderCause::Sort);
}

bool SortedFileList::isInArrivalOrder() const noexcept
{
	return _inArrivalOrder;
}

FileListSortField SortedFileList::sortField() const noexcept
{
	return _sorter.sortField();
}

Qt::SortOrder SortedFileList::sortOrder() const noexcept
{
	return _sorter.sortOrder();
}

const QString& SortedFileList::listedFolder() const noexcept
{
	return _sorter.listedFolder();
}

int SortedFileList::rowCount() const noexcept
{
	return (int)_displayedRows.size();
}

const CFileSystemObject& SortedFileList::rowAt(int row) const
{
	assert_debug_only(row >= 0 && row < (int)_displayedRows.size());
	return _rows[_displayedRows[(size_t)row]];
}

int SortedFileList::rowOf(qulonglong hash) const
{
	const auto rowIndex = _rowIndexByHash.find(hash);
	return rowIndex != _rowIndexByHash.end() ? displayRowOf(rowIndex->second) : -1;
}

std::vector<int> SortedFileList::rowsOf(const std::vector<qulonglong>& hashes) const
{
	std::vector<int> rows;
	rows.reserve(hashes.size());
	// Estimated, not measured: a search's 20 or so key comparisons cost about as much as indexing 16 rows
	if (hashes.size() * 16 < _displayedRows.size())
	{
		for (const qulonglong hash : hashes)
			rows.push_back(rowOf(hash));
		return rows;
	}

	ankerl::unordered_dense::map<qulonglong, int, identity_hash> displayRowByHash;
	displayRowByHash.reserve(_displayedRows.size());
	for (int row = 0, numRows = (int)_displayedRows.size(); row < numRows; ++row)
		displayRowByHash.emplace(_rows[_displayedRows[(size_t)row]].hash(), row);

	for (const qulonglong hash : hashes)
	{
		const auto row = displayRowByHash.find(hash);
		rows.push_back(row != displayRowByHash.end() ? row->second : -1);
	}
	return rows;
}

int SortedFileList::firstFileRow() const
{
	assert_and_return_r(!_inArrivalOrder, -1);

	const auto firstFile = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [this](uint32_t rowIndex) {
		return !isFileOrBundle(_rows[rowIndex]);
	});

	return firstFile != _displayedRows.cend() ? (int)(firstFile - _displayedRows.cbegin()) : -1;
}

const FolderContentsSummary& SortedFileList::contentsSummary() const noexcept
{
	return _contentsSummary;
}

bool SortedFileList::rowLessThan(uint32_t l, uint32_t r) const
{
	// _rows is in arrival order: an update replaces rows in place, appends new ones and closes gaps
	return _inArrivalOrder ? l < r : _sorter.lessThan(_rows[l], _sortKeys[l], _rows[r], _sortKeys[r]);
}

std::vector<uint32_t> SortedFileList::displayedRowsInOrder() const
{
	std::vector<uint32_t> displayedRows;
	displayedRows.reserve(_rows.size());
	for (uint32_t i = 0, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
	{
		if (_nameFilter.matches(_rows[i].fullName()))
			displayedRows.push_back(i);
	}

	std::sort(displayedRows.begin(), displayedRows.end(), [this](uint32_t l, uint32_t r) {
		return rowLessThan(l, r);
	});

	return displayedRows;
}

int SortedFileList::moveDestination(uint32_t rowIndex, int displayRow) const
{
	const auto sortsAboveRow = [&](uint32_t otherRowIndex) { return rowLessThan(otherRowIndex, rowIndex); };
	const auto first = _displayedRows.cbegin(), moved = first + displayRow;
	if (const auto above = std::partition_point(first, moved, sortsAboveRow); above != moved)
		return (int)(above - first);

	return (int)(std::partition_point(moved + 1, _displayedRows.cend(), sortsAboveRow) - first);
}

int SortedFileList::displayRowOf(uint32_t rowIndex) const
{
	// The sort order is total, so the search lands on the row itself when it is displayed
	const auto position = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [&](uint32_t otherRowIndex) {
		return rowLessThan(otherRowIndex, rowIndex);
	});
	return position != _displayedRows.cend() && *position == rowIndex ? (int)(position - _displayedRows.cbegin()) : -1;
}

int SortedFileList::moveRowToSortedPosition(int displayRow)
{
	const int destination = moveDestination(_displayedRows[(size_t)displayRow], displayRow);
	if (destination == displayRow || destination == displayRow + 1)
		return displayRow;

	// A move, unlike a removal and an insertion, keeps the row's selection and open editor
	const auto first = _displayedRows.begin();
	_observer.beforeMove(displayRow, destination);
	if (destination < displayRow)
		std::rotate(first + destination, first + displayRow, first + displayRow + 1);
	else
		std::rotate(first + displayRow, first + displayRow + 1, first + destination);
	_observer.afterMove();

	return destination < displayRow ? destination : destination - 1;
}

void SortedFileList::insertDisplayedRows(std::vector<uint32_t> rowIndices)
{
	std::sort(rowIndices.begin(), rowIndices.end(), [this](uint32_t l, uint32_t r) {
		return rowLessThan(l, r);
	});

	for (size_t runBegin = 0, numInserted = rowIndices.size(); runBegin < numInserted;)
	{
		const uint32_t firstInRun = rowIndices[runBegin];
		const auto position = std::partition_point(_displayedRows.cbegin(), _displayedRows.cend(), [&](uint32_t rowIndex) {
			return rowLessThan(rowIndex, firstInRun);
		});

		// The run takes in every next row that also sorts above the row at position
		size_t runEnd = runBegin + 1;
		while (runEnd < numInserted && (position == _displayedRows.cend() || rowLessThan(rowIndices[runEnd], *position)))
			++runEnd;

		const auto firstRow = (int)(position - _displayedRows.cbegin());
		_observer.beforeInsert(firstRow, firstRow + (int)(runEnd - runBegin) - 1);
		_displayedRows.insert(position, rowIndices.cbegin() + (ptrdiff_t)runBegin, rowIndices.cbegin() + (ptrdiff_t)runEnd);
		_observer.afterInsert();

		runBegin = runEnd;
	}
}

void SortedFileList::removeDisplayedRows(std::vector<int> displayRows)
{
	// Bottom up, so the rows of the runs still to remove keep their numbers
	std::sort(displayRows.begin(), displayRows.end(), std::greater{});
	for (size_t runBegin = 0, numRemoved = displayRows.size(); runBegin < numRemoved;)
	{
		size_t runEnd = runBegin + 1;
		while (runEnd < numRemoved && displayRows[runEnd] == displayRows[runEnd - 1] - 1)
			++runEnd;

		const int firstRow = displayRows[runEnd - 1], lastRow = displayRows[runBegin];
		_observer.beforeRemove(firstRow, lastRow);
		_displayedRows.erase(_displayedRows.begin() + firstRow, _displayedRows.begin() + lastRow + 1);
		_observer.afterRemove();

		runBegin = runEnd;
	}
}

void SortedFileList::changeRow(uint32_t rowIndex, CFileSystemObject&& row)
{
	CFileSystemObject& oldRow = _rows[rowIndex];
	if (!itemDataDiffers(oldRow, row))
		return;

	// Before the new data changes where the row sorts
	const int displayRow = displayRowOf(rowIndex);
	_contentsSummary.remove(oldRow);
	_contentsSummary.add(row);
	// A type change can change the name and extension the row sorts by
	oldRow = std::move(row);
	_sortKeys[rowIndex] = _sorter.sortKeysFor(oldRow);

	if (displayRow >= 0)
		_observer.rowChanged(moveRowToSortedPosition(displayRow));
}

void SortedFileList::removeRows(std::vector<uint32_t> rowIndices)
{
	std::vector<int> displayRows;
	for (const uint32_t rowIndex : rowIndices)
	{
		if (const int displayRow = displayRowOf(rowIndex); displayRow >= 0)
			displayRows.push_back(displayRow);
	}
	removeDisplayedRows(std::move(displayRows));

	// The last row moves into each gap: in sorted order, a row's index doesn't affect where it shows.
	// Highest first, so the last row is never one still to remove.
	std::sort(rowIndices.begin(), rowIndices.end(), std::greater{});
	for (const uint32_t rowIndex : rowIndices)
	{
		_contentsSummary.remove(_rows[rowIndex]);
		_rowIndexByHash.erase(_rows[rowIndex].hash());

		if (const auto lastRowIndex = (uint32_t)_rows.size() - 1; rowIndex != lastRowIndex)
		{
			const int lastDisplayRow = displayRowOf(lastRowIndex);
			_rows[rowIndex] = std::move(_rows[lastRowIndex]);
			_sortKeys[rowIndex] = std::move(_sortKeys[lastRowIndex]);
			_rowIndexByHash[_rows[rowIndex].hash()] = rowIndex;
			if (lastDisplayRow >= 0)
				_displayedRows[(size_t)lastDisplayRow] = rowIndex;
		}

		_rows.pop_back();
		_sortKeys.pop_back();
	}
}

void SortedFileList::rebuildRowIndexByHash()
{
	_rowIndexByHash.clear();
	_rowIndexByHash.reserve(_rows.size());
	for (uint32_t i = 0, numRows = (uint32_t)_rows.size(); i < numRows; ++i)
		_rowIndexByHash.emplace(_rows[i].hash(), i);
}

void SortedFileList::updateContentsSummary()
{
	_contentsSummary = {};
	for (const CFileSystemObject& row : _rows)
		_contentsSummary.add(row);
}

void SortedFileList::refilterAndResort(SortedFileListReorderCause cause)
{
	_observer.beforeReorder(cause);
	_displayedRows = displayedRowsInOrder();
	_observer.afterReorder(cause);
}

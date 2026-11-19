#include "sortedfilelist.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <functional>
#include <utility>

SortedFileList::SortedFileList(SortedFileListObserver& observer) :
	_observer(observer)
{
}

void SortedFileList::setRows(std::vector<CFileSystemObject> rows, QString listedFolder)
{
	_observer.beforeReset();

	_rows = std::move(rows);
	_sortKeys.clear();
	_sorter.clearExtensionKeyCache();
	_sorter.setListedFolder(std::move(listedFolder));
	_sortKeys.reserve(_rows.size());
	for (const CFileSystemObject& row : _rows)
		_sortKeys.push_back(_sorter.sortKeysFor(row));

	updateContentsSummary();
	_displayedRows = displayedRowsInOrder();
	_displayRowByHashIsStale = true;

	_observer.afterReset();
}

bool SortedFileList::updateRows(std::vector<CFileSystemObject> rows)
{
	const auto oldRowCount = (uint32_t)_rows.size();

	ankerl::unordered_dense::map<qulonglong, uint32_t, identity_hash> oldRowByHash;
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
			numDisplayedChanges += _nameFilter.matches(row.fullName()) ? 1 : 0;
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

		int displayRow = displayRowOfOldRow[oldRowIndex];
		if (displayRow < 0)
			continue;

		// A move, unlike a removal and an insertion, keeps the row's selection and open editor
		const int destination = moveDestination(oldRowIndex, displayRow);
		if (destination != displayRow && destination != displayRow + 1)
		{
			const auto first = _displayedRows.begin();
			_observer.beforeMove(displayRow, destination);
			if (destination < displayRow)
				std::rotate(first + destination, first + displayRow, first + displayRow + 1);
			else
				std::rotate(first + displayRow, first + displayRow + 1, first + destination);

			for (int row = std::min(displayRow, destination), end = std::max(displayRow + 1, destination); row < end; ++row)
				displayRowOfOldRow[_displayedRows[(size_t)row]] = row;

			_displayRowByHashIsStale = true;
			_observer.afterMove();
			displayRow = displayRowOfOldRow[oldRowIndex];
		}

		_observer.rowChanged(displayRow);
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
		_observer.beforeRemove(firstRow, lastRow);
		_displayedRows.erase(_displayedRows.begin() + firstRow, _displayedRows.begin() + lastRow + 1);
		_displayRowByHashIsStale = true;
		_observer.afterRemove();

		runBegin = runEnd;
	}

	std::vector<uint32_t> insertedRows;
	for (const uint32_t addedRow : addedRows)
	{
		if (_nameFilter.matches(rows[addedRow].fullName()))
			insertedRows.push_back((uint32_t)_rows.size());

		_sortKeys.push_back(_sorter.sortKeysFor(rows[addedRow]));
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
		_observer.beforeInsert(firstRow, firstRow + (int)(runEnd - runBegin) - 1);
		_displayedRows.insert(position, insertedRows.cbegin() + (ptrdiff_t)runBegin, insertedRows.cbegin() + (ptrdiff_t)runEnd);
		_displayRowByHashIsStale = true;
		_observer.afterInsert();

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
			_sortKeys[numKept] = std::move(_sortKeys[i]);
		}
		++numKept;
	}

	_rows.resize(numKept);
	// Not resize(): the keys have no default constructor
	_sortKeys.erase(_sortKeys.begin() + numKept, _sortKeys.end());
	for (uint32_t& rowIndex : _displayedRows)
		rowIndex = compactedIndex[rowIndex];

	updateContentsSummary();
	return true;
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
	if (field == _sorter.sortField() && order == _sorter.sortOrder())
		return;

	_sorter.setSorting(field, order);
	refilterAndResort(SortedFileListReorderCause::Sort);
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
	if (_displayRowByHashIsStale)
		rebuildDisplayRowByHash();

	const auto row = _displayRowByHash.find(hash);
	return row != _displayRowByHash.end() ? row->second : -1;
}

int SortedFileList::firstFileRow() const
{
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
	return _sorter.lessThan(_rows[l], _sortKeys[l], _rows[r], _sortKeys[r]);
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

void SortedFileList::rebuildDisplayRowByHash() const
{
	_displayRowByHash.clear();
	_displayRowByHash.reserve(_displayedRows.size());
	for (int row = 0, numRows = (int)_displayedRows.size(); row < numRows; ++row)
		_displayRowByHash.emplace(_rows[_displayedRows[(size_t)row]].hash(), row);

	_displayRowByHashIsStale = false;
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
	_displayRowByHashIsStale = true;
	_observer.afterReorder(cause);
}

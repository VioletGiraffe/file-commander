#pragma once

#include "filelistsorter.h"
#include "filenamefilter.h"
#include "foldercontentssummary.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QString>
#include <Qt>
RESTORE_COMPILER_WARNINGS

#include <stddef.h>
#include <stdint.h>
#include <vector>

enum class SortedFileListReorderCause { Sort, Filter, Addition };

// Receives each change to a SortedFileList's rows twice, before and after it, matching QAbstractItemModel's begin and end calls.
// Before a change, the rows are at their old positions; after it, at the new ones.
// updateRows() and applyChanges() put a changed row's new data in place before moving it, and call rowChanged() after the move.
class SortedFileListObserver
{
public:
	virtual void beforeReset() = 0;
	virtual void afterReset() = 0;
	// [first, last]: the inserted rows' numbers after the insertion
	virtual void beforeInsert(int first, int last) = 0;
	virtual void afterInsert() = 0;
	virtual void beforeRemove(int first, int last) = 0;
	virtual void afterRemove() = 0;
	// destination: the row the moved row goes above, numbered before the move: QAbstractItemModel::beginMoveRows()'s convention
	virtual void beforeMove(int row, int destination) = 0;
	virtual void afterMove() = 0;
	// The row's data changed, not its position
	virtual void rowChanged(int row) = 0;
	// Rows may move, be hidden or be added: the observer finds a row again by its hash
	virtual void beforeReorder(SortedFileListReorderCause cause) = 0;
	virtual void afterReorder(SortedFileListReorderCause cause) = 0;

protected:
	~SortedFileListObserver() = default;
};

// For an owner that reads the rows only after it finishes changing them
class NullSortedFileListObserver final : public SortedFileListObserver
{
	void beforeReset() override {}
	void afterReset() override {}
	void beforeInsert(int, int) override {}
	void afterInsert() override {}
	void beforeRemove(int, int) override {}
	void afterRemove() override {}
	void beforeMove(int, int) override {}
	void afterMove() override {}
	void rowChanged(int) override {}
	void beforeReorder(SortedFileListReorderCause) override {}
	void afterReorder(SortedFileListReorderCause) override {}
};

// A file list's items as displayed: the ones the name filter lets through, sorted by FileListSorter's rules or in arrival order.
// A row is a displayed item, numbered from the top.
class SortedFileList
{
public:
	enum class InitialOrder { Sorted, Arrival };

	// updateRows() resets past this many changed displayed rows: from there on a reset is cheaper, per the timing case in the filelist suite.
	// A count, not a share of the rows: an update's cost per change grows with the row count as a reset's does.
	// At the high end of the measured break-evens: an update keeps the scroll position and an open editor, a reset does not.
	static constexpr size_t MaxChangesForUpdate = 5000;

	// The observer must outlive this list
	explicit SortedFileList(SortedFileListObserver& observer, InitialOrder initialOrder = InitialOrder::Sorted);

	// A reset. listedFolder: see fileListFolderPrefix().
	void setRows(std::vector<CFileSystemObject> rows, QString listedFolder);
	// Removes, inserts, moves and changes single rows until the rows match 'rows', so a selection, cursor or open editor the observer
	// tracks by row number stays on its row. Returns false if it reset instead: past a number of changes, one reset is cheaper.
	// The rows are in the folder setRows() took.
	bool updateRows(std::vector<CFileSystemObject> rows);
	// Brings in added, changed and removed items row by row, as updateRows() does, at a cost that grows with the number of changes rather than of rows.
	// upserted: the added and changed items. Each hash comes at most once per call; repeating a change in a later call does nothing.
	// Requires sorted rows.
	void applyChanges(std::vector<CFileSystemObject> upserted, const std::vector<qulonglong>& removedHashes);
	// Each row where it sorts, in one reorder; none if the filter hides them all. Requires rows not listed yet.
	// One merge: linear in the rows already listed, whatever the number added.
	// In arrival order: one insertion at the end.
	void addRows(std::vector<CFileSystemObject> rows);
	// filterText: a filter field's text (see splitNameFilterField()), matched against full names ignoring case. Empty shows every row.
	void setNameFilter(const QString& filterText);
	// Ends arrival order. Re-sorts only when the sorting changes.
	void setSorting(FileListSortField field, Qt::SortOrder order);
	// The rows in the order setRows() and addRows() received them. sortField() and sortOrder() then read Name, ascending.
	void showInArrivalOrder();

	[[nodiscard]] bool isInArrivalOrder() const noexcept;
	[[nodiscard]] FileListSortField sortField() const noexcept;
	[[nodiscard]] Qt::SortOrder sortOrder() const noexcept;
	[[nodiscard]] const QString& listedFolder() const noexcept;

	[[nodiscard]] int rowCount() const noexcept;
	[[nodiscard]] const CFileSystemObject& rowAt(int row) const;
	[[nodiscard]] int rowOf(qulonglong hash) const; // -1 if no row has it, or the filter hides it
	// Parallel to hashes, each as rowOf()
	[[nodiscard]] std::vector<int> rowsOf(const std::vector<qulonglong>& hashes) const;
	// The topmost row holding a file (folders always sort above files), or -1 if there are no files. Requires sorted rows.
	[[nodiscard]] int firstFileRow() const;
	// Totals over every item, the hidden ones included
	[[nodiscard]] const FolderContentsSummary& contentsSummary() const noexcept;

private:
	// Arguments are indices into _rows
	[[nodiscard]] bool rowLessThan(uint32_t l, uint32_t r) const;
	[[nodiscard]] std::vector<uint32_t> displayedRowsInOrder() const;
	// Where _rows[rowIndex] sorts among the displayed rows other than 'displayRow', as the destination of moving displayRow
	[[nodiscard]] int moveDestination(uint32_t rowIndex, int displayRow) const;
	// -1 when the filter hides the row. Requires _displayedRows in order.
	[[nodiscard]] int displayRowOf(uint32_t rowIndex) const;
	// Moves the row at displayRow to where its sort keys place it. Returns its new display row.
	int moveRowToSortedPosition(int displayRow);
	// Indices into _rows of rows not displayed yet, in any order
	void insertDisplayedRows(std::vector<uint32_t> rowIndices);
	void removeDisplayedRows(std::vector<int> displayRows);
	void changeRow(uint32_t rowIndex, CFileSystemObject&& row);
	// Indices into _rows
	void removeRows(std::vector<uint32_t> rowIndices);
	void rebuildRowIndexByHash();
	void updateContentsSummary();
	void refilterAndResort(SortedFileListReorderCause cause);

private:
	SortedFileListObserver& _observer;
	std::vector<CFileSystemObject> _rows;
	std::vector<FileListSortKeys> _sortKeys; // Parallel to _rows
	FileListSorter _sorter; // Caches the key of every extension seen since the last setRows()
	bool _inArrivalOrder;
	QString _nameFilterText;
	FileNameFilter _nameFilter;
	// Indices into _rows: the rows the filter lets through, in display order
	std::vector<uint32_t> _displayedRows;
	// Every row's index in _rows, the hidden ones included. A display row is found by a search in _displayedRows: an index of display rows
	// would change with every insertion and removal.
	ankerl::unordered_dense::map<qulonglong, uint32_t, identity_hash> _rowIndexByHash;
	FolderContentsSummary _contentsSummary;
};

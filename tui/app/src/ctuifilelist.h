#pragma once

#include "ctuirowlist.h"

#include "cfilesystemobject.h"
#include "filelist/filelistsorter.h"

#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"

#define Uses_TView
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

// A column's cells in a row; one column per sort field
struct FileListColumn
{
	FileListSortField field;
	int x = 0, width = 0;
};
// The columns that fit in a row of `width` cells, in the order Name, Ext, Size, Date.
// Name and the sorted column are always shown; Size, Ext and Date follow, in that priority, while they fit. Name takes the width left over.
[[nodiscard]] std::vector<FileListColumn> fileListColumns(int width, FileListSortField sortField);

// The titles of the file list's columns, with an arrow for the order on the sorted one
class CTuiColumnHeader final : public TView
{
public:
	explicit CTuiColumnHeader(const TRect& bounds) noexcept;

	void setSorting(FileListSortField field, Qt::SortOrder order);
	// Called with the column whose title was clicked
	std::function<void(FileListSortField field)> onColumnClicked;

	void draw() override;
	void handleEvent(TEvent& event) override;

private:
	FileListSortField _sortField = FileListSortField::Name;
	Qt::SortOrder _sortOrder = Qt::AscendingOrder;
};

// A panel's rows
class CTuiFileList final : public CTuiRowList
{
public:
	CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	// The sorted column stays shown however narrow the list is
	void setSortField(FileListSortField field);

	// The cursor goes to the row whose hash is cursorItemHash, or to fallbackRow (clamped to the rows) when no row has it
	void setRows(std::vector<CFileSystemObject> rows, qulonglong cursorItemHash, int fallbackRow);
	[[nodiscard]] const CFileSystemObject* currentRow() const noexcept; // nullptr when there are no rows

	void setSelectedItems(const std::vector<qulonglong>& itemHashes);
	[[nodiscard]] bool isSelected(qulonglong itemHash) const noexcept;

	// Called when the user moves the cursor to another row
	std::function<void(const CFileSystemObject& row)> onCursorMoved;

	void draw() override;

private:
	[[nodiscard]] int rowCount() const noexcept override;
	void cursorMovedByUser() override;

private:
	std::vector<CFileSystemObject> _rows;
	ankerl::unordered_dense::set<qulonglong, identity_hash> _selectedItemHashes;
	FileListSortField _sortField = FileListSortField::Name;
};

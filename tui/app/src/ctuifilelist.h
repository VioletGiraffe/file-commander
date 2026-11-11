#pragma once

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

// Posted to the owner on a double click; the owner activates the item under the cursor
inline constexpr ushort cmActivateFileListItem = 1000;

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

// A panel's rows with a cursor. Not TListViewer: it counts rows in short, and a folder can hold more.
class CTuiFileList final : public TView
{
public:
	CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	// The sorted column stays shown however narrow the list is
	void setSortField(FileListSortField field);

	// The cursor goes to the row whose hash is cursorItemHash, or to fallbackRow (clamped to the rows) when no row has it
	void setRows(std::vector<CFileSystemObject> rows, qulonglong cursorItemHash, int fallbackRow);
	[[nodiscard]] const CFileSystemObject* currentRow() const noexcept; // nullptr when there are no rows
	[[nodiscard]] int cursorRow() const noexcept { return _cursorRow; }
	// Clamps row to the list
	void moveCursorTo(int row);

	void setSelectedItems(const std::vector<qulonglong>& itemHashes);
	[[nodiscard]] bool isSelected(qulonglong itemHash) const noexcept;

	// Called when the user moves the cursor to another row
	std::function<void(const CFileSystemObject& row)> onCursorMoved;

	void draw() override;
	void handleEvent(TEvent& event) override;
	void changeBounds(const TRect& bounds) override;
	void setState(ushort aState, Boolean enable) override;

private:
	// Scrolls the minimum needed to show the cursor row, then redraws
	void scrollToCursor();

private:
	std::vector<CFileSystemObject> _rows;
	ankerl::unordered_dense::set<qulonglong, identity_hash> _selectedItemHashes;
	TScrollBar* _verticalScrollBar;
	FileListSortField _sortField = FileListSortField::Name;
	int _cursorRow = 0;
	int _topRow = 0;
};

#pragma once

#include "ctuirowlist.h"

#include "cfilesystemobject.h"
#include "filelist/filelistsorter.h"
#include "filelist/filetimetext.h"
#include "filelist/sortedfilelist.h"

#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"

#define Uses_TDrawBuffer
#define Uses_TView
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <optional>
#include <vector>

// A column's cells in a row; one column per sort field
struct FileListColumn
{
	FileListSortField field;
	int x = 0, width = 0;
};
// The columns that fit in a row of `width` cells, in the order Name, Ext, Size, Date.
// Name and the sorted column are always shown; Size, Ext and Date follow, in that priority, while they fit. Name takes the width left over.
[[nodiscard]] std::vector<FileListColumn> fileListColumns(int width, FileListSortField sortField, FileTimeTextLength dateTextLength);

// What item shows in the column. extensionShown: whether the row has an Ext column; listedFolder: see fileListFolderPrefix().
[[nodiscard]] QString fileListCellText(const CFileSystemObject& item, FileListSortField field, bool extensionShown, const QString& listedFolder,
	FileTimeTextLength dateTextLength);
// Size is right-aligned, the others left-aligned
void drawFileListCell(TDrawBuffer& line, const FileListColumn& column, const QString& text, TColorAttr color);
// In the cell before each column but the first, if the GUI's setting shows them
void drawFileListColumnSeparators(TDrawBuffer& line, const std::vector<FileListColumn>& columns, TColorAttr color);

// The titles of a file list's columns, with an arrow for the order on the sorted one
class CTuiColumnHeader final : public TView
{
public:
	struct Look
	{
		FileTimeTextLength dateTextLength; // Must match the rows': it sets the columns' widths
		uchar textColourEntry, separatorColourEntry; // In the owner's palette
	};

	CTuiColumnHeader(const TRect& bounds, const Look& look) noexcept;

	void setSorting(FileListSortField field, Qt::SortOrder order);
	// No title shows an arrow, and the columns are those of a list sorted by name
	void clearSorting();
	// Called with the column whose title was clicked
	std::function<void(FileListSortField field)> onColumnClicked;

	void draw() override;
	void handleEvent(TEvent& event) override;

private:
	const Look _look;
	FileListSortField _sortField = FileListSortField::Name;
	std::optional<Qt::SortOrder> _sortOrder = Qt::AscendingOrder; // Empty after clearSorting()
};

// A panel's rows
class CTuiFileList final : public CTuiRowList
{
public:
	CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	struct Contents
	{
		std::vector<CFileSystemObject> items; // In any order
		QString listedFolder; // See fileListFolderPrefix()
		bool refreshesShownView = false; // The items are a newer listing of the view shown: unchanged rows keep their sort keys
		FileListSortField sortField = FileListSortField::Name; // This column stays shown however narrow the list is
		Qt::SortOrder sortOrder = Qt::AscendingOrder;
		QString nameFilter; // As SortedFileList::setNameFilter()
		std::vector<qulonglong> selectedItemHashes;
	};
	// The cursor goes to the row whose hash is cursorItemHash, or to fallbackRow (clamped to the rows) when no row has it.
	void setContents(Contents contents, qulonglong cursorItemHash, int fallbackRow);
	// Removes the rows and the selection. The screen keeps showing them until the next draw.
	void clearRowsKeepingScreen();
	// Draws the list that clearRowsKeepingScreen() emptied
	void showEmptyRows();
	[[nodiscard]] const CFileSystemObject* currentRow() const noexcept; // nullptr when there are no rows
	[[nodiscard]] std::optional<int> firstFileRow() const; // nullopt when no row is a file

	void setSelectedItems(const std::vector<qulonglong>& itemHashes);
	[[nodiscard]] bool isSelected(qulonglong itemHash) const noexcept;

	// Called when the user moves the cursor to another row
	std::function<void(const CFileSystemObject& row)> onCursorMoved;

	void draw() override;

private:
	[[nodiscard]] int rowCount() const noexcept override;
	void cursorMovedByUser() override;

private:
	NullSortedFileListObserver _listObserver;
	SortedFileList _list{ _listObserver };
	ankerl::unordered_dense::set<qulonglong, identity_hash> _selectedItemHashes;
};

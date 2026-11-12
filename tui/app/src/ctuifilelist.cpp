#include "ctuifilelist.h"
#include "ctuipalette.h"

#include "filelist/filesizetext.h"
#include "filelist/filetimetext.h"

#define Uses_TDrawBuffer
#define Uses_TEvent
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <array>

namespace {

constexpr int minNameColumnWidth = 20;

// Zero for Name, whose width is what the others leave
[[nodiscard]] int fixedColumnWidth(FileListSortField field) noexcept
{
	switch (field)
	{
	case FileListSortField::Name: return 0;
	case FileListSortField::Extension: return 5; // "Ext ↑"
	case FileListSortField::Size: return 6; // "Size ↑"
	case FileListSortField::Date: return maxShortFileTimeTextLength;
	}
	return 0;
}

[[nodiscard]] QString columnTitle(FileListSortField field)
{
	switch (field)
	{
	case FileListSortField::Name: return QStringLiteral("Name");
	case FileListSortField::Extension: return QStringLiteral("Ext");
	case FileListSortField::Size: return QStringLiteral("Size");
	case FileListSortField::Date: return QStringLiteral("Date");
	}
	return {};
}

// Size is right-aligned, the others left-aligned
void drawInColumn(TDrawBuffer& line, const FileListColumn& column, const QString& text, TColorAttr color)
{
	const QByteArray utf8 = text.toUtf8();
	const TStringView view{ utf8.constData(), (size_t)utf8.size() };
	const int indent = column.field == FileListSortField::Size ? column.x + std::max(column.width - strwidth(view), 0) : column.x;
	line.moveStr((ushort)indent, view, color, (ushort)column.width);
}

[[nodiscard]] QString columnText(const CFileSystemObject& item, FileListSortField field, bool extensionShown, const QString& listedFolder)
{
	switch (field)
	{
	case FileListSortField::Name: return fileListFolderPrefix(item, listedFolder) % (extensionShown ? fileListDisplayName(item) : item.fullName());
	case FileListSortField::Extension: return item.extension().toString();
	case FileListSortField::Size:
		if (item.isCdUp())
			return QStringLiteral("<UP>");
		// A folder has a size once Space has calculated it
		return item.isDir() && item.size() == 0 ? QStringLiteral("<DIR>") : compactFileSizeText(item.size());
	case FileListSortField::Date:
		return item.isCdUp() ? QString{} : fileTimeText(item.modificationTime(), FileTimeTextLength::Short);
	}
	return {};
}

} // namespace

std::vector<FileListColumn> fileListColumns(const int width, const FileListSortField sortField)
{
	// One cell before each column, and one after the last
	int nameWidth = width - 2;
	std::array<bool, 4> shown{};
	shown[(size_t)FileListSortField::Name] = true;
	const auto show = [&](FileListSortField field) {
		shown[(size_t)field] = true;
		nameWidth -= fixedColumnWidth(field) + 1;
	};

	if (sortField != FileListSortField::Name)
		show(sortField);
	for (const FileListSortField field : { FileListSortField::Size, FileListSortField::Extension, FileListSortField::Date })
	{
		if (!shown[(size_t)field] && nameWidth - fixedColumnWidth(field) - 1 >= minNameColumnWidth)
			show(field);
	}

	std::vector<FileListColumn> columns;
	int x = 1;
	for (const FileListSortField field : { FileListSortField::Name, FileListSortField::Extension, FileListSortField::Size, FileListSortField::Date })
	{
		if (!shown[(size_t)field])
			continue;

		const int columnWidth = field == FileListSortField::Name ? std::max(nameWidth, 0) : fixedColumnWidth(field);
		columns.push_back({ field, x, columnWidth });
		x += columnWidth + 1;
	}
	return columns;
}

CTuiColumnHeader::CTuiColumnHeader(const TRect& bounds) noexcept : TView(bounds)
{
	growMode = gfGrowHiX;
}

void CTuiColumnHeader::setSorting(FileListSortField field, Qt::SortOrder order)
{
	_sortField = field;
	_sortOrder = order;
	drawView();
}

void CTuiColumnHeader::draw()
{
	const TColorAttr color = mapColor(ppColumnHeader);

	TDrawBuffer line;
	line.moveChar(0, ' ', color, (ushort)size.x);
	for (const FileListColumn& column : fileListColumns(size.x, _sortField))
	{
		QString title = columnTitle(column.field);
		if (column.field == _sortField)
			title += _sortOrder == Qt::AscendingOrder ? QStringLiteral(" ↑") : QStringLiteral(" ↓");
		drawInColumn(line, column, title, color);
	}
	writeLine(0, 0, (short)size.x, 1, line);
}

void CTuiColumnHeader::handleEvent(TEvent& event)
{
	TView::handleEvent(event);
	if (event.what != evMouseDown)
		return;

	const int x = makeLocal(event.mouse.where).x;
	for (const FileListColumn& column : fileListColumns(size.x, _sortField))
	{
		if (x >= column.x && x < column.x + column.width)
		{
			onColumnClicked(column.field);
			break;
		}
	}
	clearEvent(event);
}

CTuiFileList::CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept :
	CTuiRowList(bounds, verticalScrollBar)
{
}

void CTuiFileList::setSortField(FileListSortField field)
{
	_sortField = field;
	drawView();
}

void CTuiFileList::setRows(std::vector<CFileSystemObject> rows, QString listedFolder, qulonglong cursorItemHash, int fallbackRow)
{
	_rows = std::move(rows);
	_listedFolder = std::move(listedFolder);
	const auto cursorItem = std::ranges::find(_rows, cursorItemHash, &CFileSystemObject::hash);
	rowsChanged(cursorItem != _rows.end() ? static_cast<int>(cursorItem - _rows.begin()) : fallbackRow);
}

const CFileSystemObject* CTuiFileList::currentRow() const noexcept
{
	return _rows.empty() ? nullptr : &_rows[(size_t)cursorRow()];
}

void CTuiFileList::setSelectedItems(const std::vector<qulonglong>& itemHashes)
{
	_selectedItemHashes.clear();
	_selectedItemHashes.insert(itemHashes.begin(), itemHashes.end());
	drawView();
}

bool CTuiFileList::isSelected(qulonglong itemHash) const noexcept
{
	return _selectedItemHashes.contains(itemHash);
}

void CTuiFileList::draw()
{
	const TColorAttr normalColor = mapColor(ppFileListRow), selectedColor = mapColor(ppFileListSelectedRow), cursorColor = mapColor(ppFileListCursor),
		selectedCursorColor = mapColor(ppFileListSelectedCursor);

	const std::vector<FileListColumn> columns = fileListColumns(size.x, _sortField);
	const bool extensionShown = std::ranges::any_of(columns, [](const FileListColumn& column) { return column.field == FileListSortField::Extension; });

	for (int y = 0; y < size.y; ++y)
	{
		const int row = topRow() + y;
		const bool isRow = row < (int)_rows.size();
		const bool selected = isRow && isSelected(_rows[(size_t)row].hash());
		const TColorAttr color = cursorIsShownOn(row) ? (selected ? selectedCursorColor : cursorColor) : (selected ? selectedColor : normalColor);

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		if (isRow)
		{
			for (const FileListColumn& column : columns)
				drawInColumn(line, column, columnText(_rows[(size_t)row], column.field, extensionShown, _listedFolder), color);
		}

		writeLine(0, (short)y, (short)size.x, 1, line);
	}
}

int CTuiFileList::rowCount() const noexcept
{
	return (int)_rows.size();
}

void CTuiFileList::cursorMovedByUser()
{
	if (onCursorMoved)
		onCursorMoved(_rows[(size_t)cursorRow()]);
}

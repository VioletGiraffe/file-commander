#include "ctuifilelist.h"
#include "ctuipalette.h"

#include "filelist/filetimetext.h"

#include "qtcore_helpers/displayformatting.hpp"

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
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
	case FileListSortField::Size: return 10; // "1023.9 KiB"
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

[[nodiscard]] QString columnText(const CFileSystemObject& item, FileListSortField field, bool extensionShown)
{
	switch (field)
	{
	case FileListSortField::Name: return (extensionShown ? fileListDisplayName(item) : item.fullName()).toString();
	case FileListSortField::Extension: return item.extension().toString();
	case FileListSortField::Size:
		if (item.isCdUp())
			return QStringLiteral("<UP>");
		// A folder has a size once Space has calculated it
		return item.isDir() && item.size() == 0 ? QStringLiteral("<DIR>") : fileSizeToString(item.size());
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
	TView(bounds),
	_verticalScrollBar(verticalScrollBar)
{
	options |= ofSelectable | ofFirstClick;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evBroadcast | evMouseWheel;
}

void CTuiFileList::setSortField(FileListSortField field)
{
	_sortField = field;
	drawView();
}

void CTuiFileList::setRows(std::vector<CFileSystemObject> rows, qulonglong cursorItemHash, int fallbackRow)
{
	_rows = std::move(rows);
	const auto cursorItem = std::ranges::find(_rows, cursorItemHash, &CFileSystemObject::hash);
	_cursorRow = cursorItem != _rows.end() ? static_cast<int>(cursorItem - _rows.begin()) : std::clamp(fallbackRow, 0, std::max((int)_rows.size() - 1, 0));
	scrollToCursor();
}

const CFileSystemObject* CTuiFileList::currentRow() const noexcept
{
	return _rows.empty() ? nullptr : &_rows[(size_t)_cursorRow];
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

	const bool showCursor = (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
	const std::vector<FileListColumn> columns = fileListColumns(size.x, _sortField);
	const bool extensionShown = std::ranges::any_of(columns, [](const FileListColumn& column) { return column.field == FileListSortField::Extension; });

	for (int y = 0; y < size.y; ++y)
	{
		const int row = _topRow + y;
		const bool isRow = row < (int)_rows.size();
		const bool selected = isRow && isSelected(_rows[(size_t)row].hash());
		const TColorAttr color = showCursor && row == _cursorRow ? (selected ? selectedCursorColor : cursorColor) : (selected ? selectedColor : normalColor);

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		if (isRow)
		{
			for (const FileListColumn& column : columns)
				drawInColumn(line, column, columnText(_rows[(size_t)row], column.field, extensionShown), color);
		}

		writeLine(0, (short)y, (short)size.x, 1, line);
	}
}

void CTuiFileList::handleEvent(TEvent& event)
{
	TView::handleEvent(event);

	switch (event.what)
	{
	case evMouseDown:
		moveCursorTo(_topRow + makeLocal(event.mouse.where).y);
		if (event.mouse.eventFlags & meDoubleClick)
			message(owner, evCommand, cmActivateFileListItem, this);
		clearEvent(event);
		break;
	case evMouseWheel:
		if (event.mouse.wheel == mwUp || event.mouse.wheel == mwDown)
		{
			moveCursorTo(_cursorRow + (event.mouse.wheel == mwUp ? -3 : 3));
			clearEvent(event);
		}
		break;
	case evKeyDown:
	{
		const int pageStep = std::max(size.y - 1, 1);
		switch (event.keyDown.keyCode)
		{
		case kbUp:   moveCursorTo(_cursorRow - 1); break;
		case kbDown: moveCursorTo(_cursorRow + 1); break;
		case kbPgUp: moveCursorTo(_cursorRow - pageStep); break;
		case kbPgDn: moveCursorTo(_cursorRow + pageStep); break;
		case kbHome: moveCursorTo(0); break;
		case kbEnd:  moveCursorTo((int)_rows.size() - 1); break;
		default: return;
		}
		clearEvent(event);
		break;
	}
	case evBroadcast:
		if (event.message.command == cmScrollBarChanged && event.message.infoPtr == _verticalScrollBar && _verticalScrollBar->value != _topRow)
		{
			_topRow = _verticalScrollBar->value;
			drawView();
		}
		break;
	default:
		break;
	}
}

void CTuiFileList::changeBounds(const TRect& bounds)
{
	TView::changeBounds(bounds);
	scrollToCursor();
}

void CTuiFileList::setState(ushort aState, Boolean enable)
{
	TView::setState(aState, enable);
	if (aState & (sfSelected | sfActive))
		drawView();
}

void CTuiFileList::moveCursorTo(int row)
{
	if (_rows.empty())
		return;

	row = std::clamp(row, 0, (int)_rows.size() - 1);
	const bool moved = row != _cursorRow;
	_cursorRow = row;
	scrollToCursor(); // Also when the cursor stays: the scroll bar may have scrolled it out of view
	if (moved && onCursorMoved)
		onCursorMoved(_rows[(size_t)_cursorRow]);
}

void CTuiFileList::scrollToCursor()
{
	const int visibleRows = std::max(size.y, 1);
	const int maxTopRow = std::max((int)_rows.size() - visibleRows, 0);
	_topRow = std::clamp(_topRow, std::max(_cursorRow - visibleRows + 1, 0), std::min(_cursorRow, maxTopRow));

	if (_verticalScrollBar)
		_verticalScrollBar->setParams(_topRow, 0, maxTopRow, visibleRows - 1, 1);

	drawView();
}

#include "ctuifilelist.h"

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
#define Uses_TPalette
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

// Indexes into the owning TWindow's palette: its scroller's normal and selected text
constexpr char tuiFileListPalette[] = "\x06\x07";

CTuiFileList::CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept :
	TView(bounds),
	_verticalScrollBar(verticalScrollBar)
{
	options |= ofSelectable | ofFirstClick;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evBroadcast | evMouseWheel;
}

void CTuiFileList::setRows(std::vector<CFileSystemObject> rows, qulonglong cursorItemHash)
{
	_rows = std::move(rows);
	const auto cursorItem = std::ranges::find(_rows, cursorItemHash, &CFileSystemObject::hash);
	_cursorRow = cursorItem != _rows.end() ? static_cast<int>(cursorItem - _rows.begin()) : 0;
	scrollToCursor();
}

const CFileSystemObject* CTuiFileList::currentRow() const noexcept
{
	return _rows.empty() ? nullptr : &_rows[(size_t)_cursorRow];
}

void CTuiFileList::draw()
{
	static constexpr int sizeColumnWidth = 12;

	const TColorAttr normalColor = getColor(1);
	const TColorAttr cursorColor = getColor(2);
	const bool showCursor = (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
	const bool showSizeColumn = size.x > sizeColumnWidth + 4;
	const int nameWidth = std::max(showSizeColumn ? size.x - sizeColumnWidth - 2 : size.x - 1, 0);

	for (int y = 0; y < size.y; ++y)
	{
		const int row = _topRow + y;
		const TColorAttr color = showCursor && row == _cursorRow ? cursorColor : normalColor;

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		if (row < (int)_rows.size())
		{
			const CFileSystemObject& item = _rows[(size_t)row];
			const QByteArray fileName = item.fullName().toUtf8();
			line.moveStr(1, TStringView{ fileName.constData(), (size_t)fileName.size() }, color, (ushort)nameWidth);

			if (showSizeColumn)
			{
				const QByteArray sizeText = item.isCdUp() ? QByteArrayLiteral("<UP>") : item.isDir() ? QByteArrayLiteral("<DIR>") : QByteArray::number((qulonglong)item.size());
				line.moveStr((ushort)std::max(size.x - 1 - (int)sizeText.size(), 0), TStringView{ sizeText.constData(), (size_t)sizeText.size() }, color);
			}
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

TPalette& CTuiFileList::getPalette() const
{
	static TPalette palette{ tuiFileListPalette, sizeof(tuiFileListPalette) - 1 };
	return palette;
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

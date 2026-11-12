#include "ctuirowlist.h"

#define Uses_TEvent
#define Uses_TGroup
#define Uses_TKeys
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

CTuiRowList::CTuiRowList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept :
	TView(bounds),
	_verticalScrollBar(verticalScrollBar)
{
	options |= ofSelectable | ofFirstClick;
	growMode = gfGrowHiX | gfGrowHiY;
	eventMask |= evBroadcast | evMouseWheel;
}

void CTuiRowList::handleEvent(TEvent& event)
{
	TView::handleEvent(event);

	switch (event.what)
	{
	case evMouseDown:
		moveCursorTo(_topRow + makeLocal(event.mouse.where).y);
		if (event.mouse.eventFlags & meDoubleClick)
			message(owner, evCommand, cmActivateRowUnderCursor, this);
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
		case kbEnd:  moveCursorTo(rowCount() - 1); break;
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

void CTuiRowList::changeBounds(const TRect& bounds)
{
	TView::changeBounds(bounds);
	scrollToCursor();
}

void CTuiRowList::setState(ushort aState, Boolean enable)
{
	TView::setState(aState, enable);
	if (aState & (sfSelected | sfActive))
		drawView();
}

void CTuiRowList::moveCursorTo(int row)
{
	if (rowCount() == 0)
		return;

	row = std::clamp(row, 0, rowCount() - 1);
	const bool moved = row != _cursorRow;
	_cursorRow = row;
	scrollToCursor(); // Also when the cursor stays: the scroll bar may have scrolled it out of view
	if (moved)
		cursorMovedByUser();
}

void CTuiRowList::rowsChanged(const int cursorRow)
{
	_cursorRow = std::clamp(cursorRow, 0, std::max(rowCount() - 1, 0));
	scrollToCursor();
}

bool CTuiRowList::cursorIsShownOn(const int row) const noexcept
{
	return row == _cursorRow && (state & (sfSelected | sfActive)) == (sfSelected | sfActive);
}

void CTuiRowList::scrollToCursor()
{
	const int visibleRows = std::max(size.y, 1);
	const int maxTopRow = std::max(rowCount() - visibleRows, 0);
	_topRow = std::clamp(_topRow, std::max(_cursorRow - visibleRows + 1, 0), std::min(_cursorRow, maxTopRow));

	if (_verticalScrollBar)
		_verticalScrollBar->setParams(_topRow, 0, maxTopRow, visibleRows - 1, 1);

	drawView();
}

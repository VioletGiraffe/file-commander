#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TView
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

// Posted to the owner on a double click; the owner activates the row under the cursor
inline constexpr ushort cmActivateRowUnderCursor = 1000;

// Clears a text key without Alt or Ctrl. A list in a dialog calls it after handling a key: the dialog's buttons and check boxes
// also take their letters without Alt, from a focused view that leaves the key.
void clearPlainTextKey(TEvent& event);

// Rows with a cursor, scrolled to keep it in view. Not TListViewer: it counts rows in short, and a folder can hold more.
// A derived class holds the rows and draws them.
class CTuiRowList : public TView
{
public:
	CTuiRowList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	[[nodiscard]] int cursorRow() const noexcept { return _cursorRow; }
	// Clamps row to the list
	void moveCursorTo(int row);
	// Must be called for a press the owner window consumes to take the focus: the list does not receive it.
	void recordFirstClick(const TEvent& mouseDown);

	void handleEvent(TEvent& event) override;
	void changeBounds(const TRect& bounds) override;
	void setState(ushort aState, Boolean enable) override;

protected:
	[[nodiscard]] virtual int rowCount() const noexcept = 0;
	// Called when the user moves the cursor to another row
	virtual void cursorMovedByUser() {}
	// Called after a click on a row, once the cursor is on it
	virtual void rowClicked() {}

	// Must be called after the rows change. Puts the cursor on cursorRow, clamped to the rows, and redraws.
	void rowsChanged(int cursorRow);
	// rowsChanged() for rows where cursorRow holds the item the cursor was on: a double click on that item stays possible
	void rowsChangedKeepingCursorItem(int cursorRow);
	// rowsChangedKeepingCursorItem() for added rows. Scrolls only if the cursor was on screen: a view the scroll bar moved stays there.
	void rowsAdded(int cursorRow);
	// Must be called after emptying the rows when they stay on screen until the next draw. The scroll bar stays until rowsChanged().
	void rowsEmptiedWithoutRedraw() noexcept
	{
		_cursorRow = _topRow = 0;
		_firstClickRow = noRow;
	}

	// For draw(): the row on the view's first line, and whether the row is drawn as the cursor's
	[[nodiscard]] int topRow() const noexcept { return _topRow; }
	[[nodiscard]] bool cursorIsShownOn(int row) const noexcept;

private:
	// Scrolls the minimum needed to show the cursor row, then redraws
	void scrollToCursor();
	void updateScrollBarAndRedraw();

private:
	static constexpr int noRow = -1;

	TScrollBar* _verticalScrollBar;
	int _cursorRow = 0;
	int _topRow = 0;
	// The row of the last left press: Turbo Vision flags a double click by button, screen cell and time, even when another view took the first press
	int _firstClickRow = noRow;
};

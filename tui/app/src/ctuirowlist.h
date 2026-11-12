#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TView
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

// Posted to the owner on a double click; the owner activates the row under the cursor
inline constexpr ushort cmActivateRowUnderCursor = 1000;

// Rows with a cursor, scrolled to keep it in view. Not TListViewer: it counts rows in short, and a folder can hold more.
// A derived class holds the rows and draws them.
class CTuiRowList : public TView
{
public:
	CTuiRowList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	[[nodiscard]] int cursorRow() const noexcept { return _cursorRow; }
	// Clamps row to the list
	void moveCursorTo(int row);

	void handleEvent(TEvent& event) override;
	void changeBounds(const TRect& bounds) override;
	void setState(ushort aState, Boolean enable) override;

protected:
	[[nodiscard]] virtual int rowCount() const noexcept = 0;
	// Called when the user moves the cursor to another row
	virtual void cursorMovedByUser() {}

	// Must be called after the rows change. Puts the cursor on cursorRow, clamped to the rows, and redraws.
	void rowsChanged(int cursorRow);

	// For draw(): the row on the view's first line, and whether the row is drawn as the cursor's
	[[nodiscard]] int topRow() const noexcept { return _topRow; }
	[[nodiscard]] bool cursorIsShownOn(int row) const noexcept;

private:
	// Scrolls the minimum needed to show the cursor row, then redraws
	void scrollToCursor();

private:
	TScrollBar* _verticalScrollBar;
	int _cursorRow = 0;
	int _topRow = 0;
};

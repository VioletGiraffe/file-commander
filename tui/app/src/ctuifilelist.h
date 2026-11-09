#pragma once

#include "cfilesystemobject.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TView
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

// Posted to the owner on a double click; the owner activates the item under the cursor
inline constexpr ushort cmActivateFileListItem = 1000;

// A panel's rows with a cursor. Not TListViewer: it counts rows in short, and a folder can hold more.
class CTuiFileList final : public TView
{
public:
	CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept;

	// The cursor goes to the row whose hash is cursorItemHash, or to the first row
	void setRows(std::vector<CFileSystemObject> rows, qulonglong cursorItemHash);
	[[nodiscard]] const CFileSystemObject* currentRow() const noexcept; // nullptr when there are no rows

	// Called when the user moves the cursor to another row
	std::function<void(const CFileSystemObject& row)> onCursorMoved;

	void draw() override;
	void handleEvent(TEvent& event) override;
	void changeBounds(const TRect& bounds) override;
	void setState(ushort aState, Boolean enable) override;
	TPalette& getPalette() const override;

private:
	void moveCursorTo(int row);
	// Scrolls the minimum needed to show the cursor row, then redraws
	void scrollToCursor();

private:
	std::vector<CFileSystemObject> _rows;
	TScrollBar* _verticalScrollBar;
	int _cursorRow = 0;
	int _topRow = 0;
};

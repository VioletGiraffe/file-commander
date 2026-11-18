#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QColor>
RESTORE_COMPILER_WARNINGS

// The shape of the file list's rows. The colours come from the widget palette, except those given here.
struct FileListLook
{
	// A row's selection or cursor fill is inset from the view's side edges and stops short of the next row
	int rowFillInset = 6, rowFillGap = 1;
	int rowFillRadius = 6;
	// Between the fill's edges and the row's content
	int contentHMargin = 6, contentVMargin = 4;

	// The active panel's cursor.
	// Tint: an accent-tinted fill with an accent outline; a selected row keeps its selection fill.
	// Bar: a solid accent fill, over a selected row too.
	enum class Cursor { Tint, Bar };
	Cursor cursor = Cursor::Tint;

	// The other panel's cursor. Fill: a selected row keeps its selection fill and gets the outline. Invalid colour: the text colour, faint.
	enum class InactiveCursor { Outline, Fill };
	InactiveCursor inactiveCursor = InactiveCursor::Outline;
	QColor inactiveCursorColor;

	// The active panel's background. Invalid: the palette's Base, as in the other panel.
	QColor activeBackground;

	// The column separators. Invalid: the text colour, faint.
	QColor separatorColor;
	bool separatorsDashed = true;

	// A line under every row. Invalid: none.
	QColor rowSeparatorColor;

	// The rows lie in bands of rowsPerBand rows. The bands and the row lines continue below the last row.
	// Every second band is filled with this colour. Invalid: none is.
	QColor rowBandColor;
	int rowsPerBand = 1;
	bool rowBandsInActivePanelOnly = false;
	// A line under the last row of every band, in place of rowSeparatorColor's. Invalid: none.
	QColor rowBandSeparatorColor;
	int rowBandSeparatorWidth = 1;
	// The fill of the rows outside the filled bands. Valid: every row is a strip of its own, filled down to rowFillGap above the next row.
	QColor rowStripColor;

	// A vertical line between the icons and the names, which move apart for it. Invalid: none.
	QColor marginLineColor;

	bool uppercaseHeader = false; // with the letters spaced out
};

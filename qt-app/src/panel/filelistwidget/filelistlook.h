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

	// The column separators. Invalid: the text colour, faint.
	QColor separatorColor;
	bool separatorsDashed = true;
};

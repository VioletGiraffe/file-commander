#pragma once

// The shape of the file list's rows. The colours come from the widget palette.
struct FileListLook
{
	// A row's selection or cursor fill is inset from the view's side edges and stops short of the next row
	int rowFillInset = 6, rowFillGap = 1;
	int rowFillRadius = 6;
	// Between the fill's edges and the row's content
	int contentHMargin = 6, contentVMargin = 4;
};

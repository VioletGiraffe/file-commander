#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TPalette
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

// The panel window's palette: the blue window's eight entries, then these.
// The panel's own views pass them to mapColor: a view without a palette hands its index to the owner unchanged.
enum PanelPaletteEntry : uchar
{
	ppFileListRow = 9,
	ppFileListSelectedRow,
	ppFileListCursor,
	ppFileListSelectedCursor,
	ppColumnHeader,
	ppStatusText, // The panel's status line and the tab strip's inactive tabs
	ppActiveTab,
	ppFilterText,
	ppFilterSelectedText,
	ppFilterArrows,
	ppLastEntry = ppFilterArrows
};

// Turbo Vision's application palette in RGB, followed by the panel's entries
[[nodiscard]] TPalette& applicationPalette();
[[nodiscard]] TPalette& panelPalette();

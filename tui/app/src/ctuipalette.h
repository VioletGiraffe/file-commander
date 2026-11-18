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
	ppFileListRowDetails, // Every column but Name
	ppFileListSelectedRow,
	ppFileListBandRow, // The three entries above, for a row on the theme's rowBandBack
	ppFileListBandRowDetails,
	ppFileListBandSelectedRow,
	ppFileListCursor,
	ppFileListSelectedCursor,
	ppColumnHeader,
	ppStatusText, // The panel's status line and the tab strip's inactive tabs
	ppActiveTab,
	ppFilterText,
	ppFilterSelectedText,
	ppFilterArrows,
	ppColumnSeparator, // In the rows and the column header
	ppBandColumnSeparator,
	ppCursorColumnSeparator,
	ppLastEntry = ppCursorColumnSeparator
};

struct ColourTheme;

// Sets the application palette's colours. Views read them at every draw: the caller redraws.
// Keeps a reference to the theme: it must be one of colourThemes().
void applyColourTheme(const ColourTheme& theme);
// The default theme until applyColourTheme sets another
[[nodiscard]] const ColourTheme& appliedColourTheme();

// Turbo Vision's application palette in RGB, followed by the entries of the panel, the hint row and the key bar.
// The default theme's colours until applyColourTheme sets another theme.
[[nodiscard]] TPalette& applicationPalette();
[[nodiscard]] TPalette& panelPalette();
// The rows under the panels: entries 1-6 are in Turbo Vision's status line order, and the key bar's entry 7 is its background
[[nodiscard]] TPalette& hintLinePalette();
[[nodiscard]] TPalette& keyBarPalette();

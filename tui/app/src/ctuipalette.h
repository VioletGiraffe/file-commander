#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TPalette
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <string_view>

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
	ppColumnSeparator, // In the rows and the column header
	ppCursorColumnSeparator,
	ppLastEntry = ppCursorColumnSeparator
};

enum class ColourTheme : uchar { ClassicBlue, Dark, Amber, PurpleNight, DeepTeal, Matrix, Slate, Ember, Midnight, Light, CoolGrey, LightCyan, Solar, Mint, Blossom };
inline constexpr size_t colourThemeCount = 15;

// As the menu shows it: the shortcut letter is between tildes
[[nodiscard]] const char* colourThemeMenuName(ColourTheme theme);
// The settings store it: never changes
[[nodiscard]] const char* colourThemeId(ColourTheme theme);
// Empty for an unknown id
[[nodiscard]] std::optional<ColourTheme> colourThemeWithId(std::string_view id);
// Sets the application palette's colours. Views read them at every draw: the caller redraws.
void applyColourTheme(ColourTheme theme);

// Turbo Vision's application palette in RGB, followed by the entries of the panel, the hint row and the key bar.
// Classic blue until applyColourTheme sets another theme.
[[nodiscard]] TPalette& applicationPalette();
[[nodiscard]] TPalette& panelPalette();
// The rows under the panels: entries 1-6 are in Turbo Vision's status line order, and the key bar's entry 7 is its background
[[nodiscard]] TPalette& hintLinePalette();
[[nodiscard]] TPalette& keyBarPalette();

#pragma once

#include "panel/filelistwidget/filelistlook.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "theme/cbasepalette.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <vector>

class QApplication;

// Geometry of the stylesheet and of the layouts. Per theme, so that a theme can reshape and not only recolour.
struct ThemeMetrics
{
	int controlRadius = 7; // buttons, fields, combo boxes, group boxes
	int panelRadius = 10;
	int menuItemRadius = 5;
	int scrollBarThickness = 10;

	// A file panel: between its border and the rows other than the file list, and between its rows
	int panelPadding = 8, panelRowSpacing = 6;
	// The main window: beside the panels and the command line, between the panels
	int windowMargin = 8, panelGap = 8;
	// The function key bar: between its buttons, and between it and the window's edges
	int functionKeySpacing = 6, functionKeyBarMargin = 8;
};

// What a theme changes besides colours and sizes
enum class ThemeStructure
{
	Rounded, // panels are cards on the window background; controls have rounded corners
	Flat     // line frames and filled cells, square corners, a monospace font in every control
};

// One selectable look. `name` is unique within its polarity; a look retuned for the other polarity keeps its name.
struct Theme
{
	QString name;
	bool dark = false;
	ThemeStructure structure = ThemeStructure::Rounded;
	CBasePalette palette;
	ThemeMetrics metrics;
	FileListLook fileList;
};

// The first theme of each polarity is that polarity's default.
[[nodiscard]] const std::vector<Theme>& allThemes();

// Sets the application style, then applies the theme selected in CThemeController, now and whenever the selection changes.
// Call once, before any widget exists.
void installTheme(QApplication& app);

// The theme in effect, its palette resolved. Requires installTheme().
// For what the application palette and stylesheet do not carry: layout geometry, the file list's shape, colours without a QPalette role.
// Already updated when a slot connected to CThemeController::themeChanged after installTheme() runs.
[[nodiscard]] const Theme& appliedTheme();

// Tablet mode enlarges the application font for touch, and applies the theme again.
// CThemeController::themeChanged is not emitted: the caller must set again any font it gave a widget itself.
void setTabletMode(bool enabled);
// The point size of every font in tablet mode; empty outside it
[[nodiscard]] std::optional<int> tabletModeFontPointSize();

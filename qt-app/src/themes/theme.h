#pragma once

#include "functionkeybar/functionkeyform.h"
#include "panel/filelistwidget/filelistlook.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "theme/cbasepalette.h"


DISABLE_COMPILER_WARNINGS
#include <QColor>
#include <QFont>
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
	int panelBorderWidth = 1; // Rounded only: a flat panel's frame is Theme::panelFrame
	int menuItemRadius = 5;
	int scrollBarThickness = 10;

	// A file panel: between its border and the rows other than the file list, and between its rows
	int panelPadding = 8, panelRowSpacing = 6;
	// The main window: beside the panels, between them, and beside the command line
	int windowMargin = 8, panelGap = 8, commandLineMargin = 8;
	// The function key bar: between its buttons, and between it and the window's edges
	int functionKeySpacing = 6, functionKeyBarMargin = 8;
	// Between drive buttons, across and down. A click in the gap hits no button.
	int driveButtonSpacing = 0;
	// Between the panel's tabs, across and down
	int tabSpacing = 0;

	// PathBarStyle::Titled: the current folder's text size relative to the other folders'
	qreal pathTitleScale = 1.6;
	// A halo in their own colour around the active flat panel's frame, the cursor bar, the path's current folder, the key caps
	// and the command line's block cursor, fading out over this many px. 0: none.
	int glowRadius = 0;
};

// What a theme changes besides colours and sizes
enum class ThemeStructure
{
	Rounded, // panels are cards on the window background; controls have rounded corners
	Flat     // line frames and filled cells, square corners
};

// How CBreadcrumbBar draws the path
enum class PathBarStyle
{
	Field, // a framed field
	Strip, // Flat structure only: an opaque cell in the frame's top edge, filled with the accent in the active panel
	Titled // dim text on the panel; the current folder is a title, ThemeMetrics::pathTitleScale times as large, in the accent colour in the active panel
};

// The order of a panel's rows, and where its frame runs. A flat layout's frame is drawn by CPanelWidget.
enum class PanelLayout
{
	Stacked,     // Rounded structure: drives, path, tabs, the file list, the folder summary
	PathInFrame, // Flat structure: the path row in the frame's top edge, then drives, tabs, the file list, the summary
	// Flat structure: the tabs stand on the frame, the current one open into it.
	// Inside: the path, the file list, then the drives and the summary below a line.
	TabsOnFrame
};

// The lines of a flat panel's frame, each 1 px
enum class PanelFrame
{
	Single,
	Double
};

// One selectable look. `name` is unique within its polarity; a look retuned for the other polarity keeps its name.
struct Theme
{
	QString name;
	bool dark = false;
	ThemeStructure structure = ThemeStructure::Rounded;
	bool monospaceControls = false; // the file list's font is the user's choice either way
	PanelLayout panelLayout = PanelLayout::Stacked;
	PathBarStyle pathBar = PathBarStyle::Field;
	PanelFrame panelFrame = PanelFrame::Single; // Flat structure only
	FunctionKeyForm functionKeys = FunctionKeyForm::KeyBox;
	CBasePalette palette;
	ThemeMetrics metrics;
	FileListLook fileList;
	// The menu bar's mnemonic letters. Invalid: drawn as the rest of the label.
	QColor menuBarMnemonicColor;
	// The numbers in a panel's folder summary. Invalid: in the summary's text colour.
	QColor summaryValueColor;
	// The command line's cursor is a block in the accent colour, the character under it in the surface colour
	bool commandLineBlockCursor = false;
	// Appended after the stylesheet sections the structure, the panel layout and the path bar select, so its rules win at equal specificity.
	// Takes the same @tokens@.
	// Must colour a drive button through a selector containing "#_driveButtonsWidget > QPushButton": the glyphs follow those rules.
	const char* styleSheet = nullptr;
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

// The font saved for the file list; a monospace font at the application's size where none is saved or the saved family is not installed.
// Requires installTheme(). Ignores tablet mode.
[[nodiscard]] QFont fileListFont();

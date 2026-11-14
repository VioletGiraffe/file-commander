#include "stylesheet.h"
#include "theme.h"


// Submodule includes
#include "theme/colorutils.h"
#include "theme/cstylesheettokens.h"
#include "theme/cthemeiconhandler.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// A widget is drawn either by the base style or from its rules, never by both: a rule that sets a border or a
// background takes the whole widget, sub-controls included. Each section therefore styles its control completely.
// Controls with no section here are drawn by the base style from the palette.

namespace {

// outline: none - the focused button is marked by its border; without it the base style adds its own focus frame
constexpr char kPushButtons[] = R"(
	QPushButton {
		background: @surface@;
		border: 1px solid @border@;
		border-radius: @controlRadius@px;
		padding: 5px 9px;
		outline: none;
	}
	QPushButton:hover { border-color: @borderStrong@; }
	QPushButton:default, QPushButton:focus { border-color: @accent@; }
	QPushButton:pressed, QPushButton:checked { background: @accentBg@; border-color: @accent@; }
	QPushButton:disabled { color: @textDim@; border-color: @borderSubtle@; }
)";

constexpr char kToolButtons[] = R"(
	QToolButton {
		background: transparent;
		border: 1px solid transparent;
		border-radius: @controlRadius@px;
		padding: 2px;
	}
	QToolButton:hover { background: @surface@; border-color: @border@; }
	QToolButton:pressed, QToolButton:checked { background: @accentBg@; border-color: @accent@; }
)";

// The line edit inside a combo box, a spin box or an item view is framed by its host.
constexpr char kLineEdits[] = R"(
	QLineEdit {
		background: palette(base);
		border: 1px solid @border@;
		border-radius: @controlRadius@px;
		padding: 4px 8px;
		selection-background-color: @selectionBg@;
		selection-color: @selectionFg@;
	}
	QLineEdit:focus { border-color: @accent@; }
	QLineEdit:disabled { color: @textDim@; }

	QComboBox QLineEdit, QAbstractSpinBox QLineEdit { border: none; border-radius: 0; padding: 0; background: transparent; }
	QAbstractItemView QLineEdit { border: 1px solid @accent@; border-radius: 3px; padding: 0 1px; }
)";

// ::drop-down and ::down-arrow go together: without them the stylesheet engine draws a bevelled arrow button itself.
// ::item padding: a styled popup loses the base style's item margins.
constexpr char kComboBoxes[] = R"(
	QComboBox {
		background: @surface@;
		border: 1px solid @border@;
		border-radius: @controlRadius@px;
		padding: 4px 8px;
	}
	QComboBox:hover { border-color: @borderStrong@; }
	QComboBox:focus, QComboBox:on { border-color: @accent@; }
	QComboBox::drop-down {
		subcontrol-origin: padding;
		subcontrol-position: center right;
		border: none;
		background: transparent;
		width: 18px;
	}
	QComboBox::down-arrow { image: url(@comboArrowIcon@); width: 10px; height: 7px; }
	QComboBox QAbstractItemView {
		background: palette(base);
		border: 1px solid @border@;
		outline: none;
		selection-background-color: @selectionBg@;
		selection-color: @selectionFg@;
	}
	QComboBox QAbstractItemView::item { padding: 4px 8px; }
)";

// No radius on the menu itself: a popup window's corners stay square behind a rounded border.
constexpr char kMenus[] = R"(
	QMenu { background: @surface@; border: 1px solid @border@; padding: 4px; }
	QMenu::item { border-radius: @menuItemRadius@px; padding: 5px 24px 5px 12px; }
	QMenu::item:selected { background: @accentBg@; color: @accentText@; }
	QMenu::item:disabled { color: @textDim@; }
	QMenu::separator { height: 1px; background: @borderSubtle@; margin: 4px 8px; }

	QMenuBar { background: transparent; }
	QMenuBar::item { background: transparent; border-radius: @menuItemRadius@px; padding: 4px 9px; }
	QMenuBar::item:selected, QMenuBar::item:pressed { background: @accentBg@; color: @accentText@; }
)";

constexpr char kScrollBars[] = R"(
	QScrollBar:vertical { background: transparent; border: none; width: @scrollBarThickness@px; margin: 0; }
	QScrollBar::handle:vertical { background: @borderStrong@; border-radius: @scrollBarHandleRadius@px; min-height: 24px; }
	QScrollBar::handle:vertical:hover { background: @textDim@; }
	QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
	QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

	QScrollBar:horizontal { background: transparent; border: none; height: @scrollBarThickness@px; margin: 0; }
	QScrollBar::handle:horizontal { background: @borderStrong@; border-radius: @scrollBarHandleRadius@px; min-width: 24px; }
	QScrollBar::handle:horizontal:hover { background: @textDim@; }
	QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
	QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
)";

constexpr char kGroupBoxes[] = R"(
	QGroupBox {
		border: 1px solid @border@;
		border-radius: @controlRadius@px;
		margin-top: 1ex;
		padding-top: 4px;
	}
	QGroupBox::title {
		subcontrol-origin: margin;
		subcontrol-position: top left;
		left: 8px;
		padding: 0 3px;
		background-color: palette(window);
		color: @textDim@;
	}
)";

constexpr char kTabs[] = R"(
	QTabBar::tab {
		background: transparent;
		border: none;
		border-bottom: 2px solid transparent;
		padding: 6px 12px;
		color: @textDim@;
	}
	QTabBar::tab:selected { color: @text@; border-bottom-color: @accent@; }
	QTabBar::tab:hover:!selected { color: @text@; }
	QTabBar::close-button { image: url(@tabCloseIcon@); }
	QTabBar::close-button:hover { image: url(@tabCloseHoverIcon@); }
	QTabWidget::pane { border: 1px solid @border@; border-radius: @controlRadius@px; }
)";

constexpr char kHeaders[] = R"(
	QHeaderView { background: transparent; }
	QHeaderView::section {
		background: palette(base);
		color: @textDim@;
		border: none;
		border-bottom: 1px solid @border@;
		padding: 5px 8px;
	}
)";

constexpr char kToolTips[] = R"(
	QToolTip { background-color: @surface@; color: @text@; border: 1px solid @border@; padding: 4px 8px; }
)";

// The text is drawn in one colour across the chunk and the trough, so the chunk is a tint that keeps it readable
constexpr char kProgressBars[] = R"(
	QProgressBar {
		background: @surface@;
		border: 1px solid @border@;
		border-radius: @controlRadius@px;
		text-align: center;
	}
	QProgressBar::chunk { background: @progressChunk@; border-radius: @progressChunkRadius@px; }
)";

// A background on the handle keeps the base style from drawing its grip dots
constexpr char kSplitters[] = R"(
	QSplitter::handle { background: palette(window); }
)";

// A file panel is a card on the window background. CPanelWidget sets the activePanel property.
constexpr char kPanels[] = R"(
	CPanelWidget { background: @surface@; border: 1px solid @border@; border-radius: @panelRadius@px; }
	CPanelWidget[activePanel="true"] { border-color: @panelActiveBorder@; }

	#_driveInfoLabel, #_infoLabel { color: @textDim@; }
	CPanelWidget > QComboBox { background: @windowBg@; }
	CPanelWidget > QToolButton { border-color: @border@; }
	CPanelWidget > QToolButton:hover { background: @surfaceAlt@; border-color: @borderStrong@; }
	CPanelWidget > QToolButton:pressed { background: @accentBg@; border-color: @accent@; }

	#_driveButtonsWidget > QPushButton {
		background: transparent;
		border: 1px solid transparent;
		border-radius: @controlRadius@px;
		padding: 3px 8px;
		color: @textDim@;
	}
	#_driveButtonsWidget > QPushButton:hover { background: @surfaceAlt@; color: @text@; }
	#_driveButtonsWidget > QPushButton:pressed, #_driveButtonsWidget > QPushButton:checked { background: @accentBg@; color: @accentText@; }
)";

// The command line row and the function key bar
constexpr char kMainWindow[] = R"(
	CMainWindow > #centralwidget > QLabel { color: @textDim@; }
	CMainWindow > #centralwidget > QPushButton { padding: 7px 9px; }
)";

} // namespace

QString buildStyleSheet(const Theme& theme)
{
	QString sheet;
	for (const char* section : { kPushButtons, kToolButtons, kLineEdits, kComboBoxes, kMenus, kScrollBars, kGroupBoxes, kTabs,
	                             kHeaders, kToolTips, kProgressBars, kSplitters, kPanels, kMainWindow })
		sheet += QLatin1String(section);

	const CBasePalette& palette = theme.palette;
	const ThemeMetrics& metrics = theme.metrics;

	CStyleSheetTokens tokens;
	tokens.addPalette(palette);
	tokens.add(QStringLiteral("controlRadius"), metrics.controlRadius);
	tokens.add(QStringLiteral("panelRadius"), metrics.panelRadius);
	tokens.add(QStringLiteral("panelActiveBorder"), ColorUtils::mix(palette.border, palette.accent, 0.55f));
	tokens.add(QStringLiteral("menuItemRadius"), metrics.menuItemRadius);
	tokens.add(QStringLiteral("scrollBarThickness"), metrics.scrollBarThickness);
	tokens.add(QStringLiteral("scrollBarHandleRadius"), metrics.scrollBarThickness / 2);
	tokens.add(QStringLiteral("progressChunk"), ColorUtils::mix(palette.surface, palette.accent, 0.45f));
	tokens.add(QStringLiteral("progressChunkRadius"), metrics.controlRadius - 1);
	tokens.add(QStringLiteral("comboArrowIcon"), themeIconUrl(QStringLiteral("combobox_down_arrow"), palette.textDim));
	tokens.add(QStringLiteral("tabCloseIcon"), themeIconUrl(QStringLiteral("tab_close"), palette.textDim));
	tokens.add(QStringLiteral("tabCloseHoverIcon"), themeIconUrl(QStringLiteral("tab_close"), palette.text));

	return tokens.expanded(sheet);
}

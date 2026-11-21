#include "theme.h"
#include "stylesheet.h"

#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "theme/colorutils.h"
#include "theme/cstylefixups.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLatin1String>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
RESTORE_COMPILER_WARNINGS

#include <optional>

namespace {

QColor rgb(QRgb value) { return QColor::fromRgb(value); }

Theme g_appliedTheme;
// The application font the themes start from. Set by installTheme(): a QFont needs the application object.
std::optional<QFont> g_baseFont;
bool g_tabletMode = false;
constexpr int tabletFontPointSize = 24;

// The first installed family of a list of well-drawn ones; the system's fixed font if none is
[[nodiscard]] QString monospaceFamily()
{
	for (const char* family : { "Cascadia Mono", "Consolas", "Menlo", "DejaVu Sans Mono", "Liberation Mono" })
	{
		if (QFontDatabase::hasFamily(QLatin1String{ family }))
			return QLatin1String{ family };
	}

	return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

// The flat structure with monospace controls. The column lines take the palette's borderSubtle, which must be authored: the palette is not resolved yet.
[[nodiscard]] Theme flatMonospaceTheme(const QString& name, const bool dark, const CBasePalette& palette)
{
	return {
		.name = name,
		.dark = dark,
		.structure = ThemeStructure::Flat,
		.monospaceControls = true,
		.panelLayout = PanelLayout::PathInFrame,
		.pathBar = PathBarStyle::Strip,
		.functionKeys = FunctionKeyForm::LabelCell,
		.palette = palette,
		.metrics = {
			.controlRadius = 0, .panelRadius = 0, .menuItemRadius = 0,
			.panelPadding = 10,
			.windowMargin = 10, .panelGap = 10, .commandLineMargin = 10,
			.functionKeySpacing = 0, .functionKeyBarMargin = 0,
			.tabSpacing = 8,
		},
		.fileList = {
			.rowFillInset = 0, .rowFillGap = 0,
			.rowFillRadius = 0,
			.contentHMargin = 8, .contentVMargin = 3,
			.cursor = FileListLook::Cursor::Bar,
			.separatorColor = palette.borderSubtle,
			.separatorsDashed = false,
		},
	};
}

// Blueprint in one polarity: everything but the palette is shared. Graph paper: bands of five rows, each closed by a grid line.
[[nodiscard]] Theme blueprintTheme(const bool dark, const CBasePalette& palette)
{
	Theme theme = flatMonospaceTheme(QStringLiteral("Blueprint"), dark, palette);
	theme.fileList.rowBandColor = ColorUtils::mix(palette.surface, palette.surfaceAlt, 0.5f);
	theme.fileList.rowsPerBand = 5;
	theme.fileList.rowBandSeparatorColor = palette.borderSubtle;
	return theme;
}

// Green bar printer paper: bands of three rows
[[nodiscard]] Theme greenbarTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0xedf3ea), .surface = rgb(0xfafaf2), .surfaceAlt = rgb(0xdcebd9),
		.text = rgb(0x23302a), .textDim = rgb(0x5a6b60),
		.button = rgb(0xedf3ea),
		.accent = rgb(0x2e6b4a),
		.selectionBg = rgb(0xfbe0cc), .selectionFg = rgb(0x9a3a12),
		.border = rgb(0xb5cdb3), .borderSubtle = rgb(0xc3d8c1),
		.accentFg = rgb(0xffffff),
	};

	Theme theme = flatMonospaceTheme(QStringLiteral("Greenbar"), false, palette);
	theme.fileList.rowBandColor = palette.surfaceAlt;
	theme.fileList.rowsPerBand = 3;
	return theme;
}

// A split-flap board: every row is a strip in one of two greys, with the window's black between the strips and between their columns.
// Names are signal yellow; the dim text is near white, as the board's second colour.
[[nodiscard]] Theme departuresTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0x050505), .surface = rgb(0x050505), .surfaceAlt = rgb(0x2a2a2a),
		.text = rgb(0xffd21f), .textDim = rgb(0xd0d0d0),
		.button = rgb(0x171717),
		.accent = rgb(0xffd21f),
		.selectionBg = rgb(0x12351f), .selectionFg = rgb(0x6ee7a0),
		.border = rgb(0x2e2e2e), .borderSubtle = rgb(0x1e1e1e),
		.accentFg = rgb(0x0a0a0a),
	};

	Theme theme = flatMonospaceTheme(QStringLiteral("Departures"), true, palette);
	FileListLook& look = theme.fileList;
	look.rowFillGap = 2;
	look.contentVMargin = 4;
	look.separatorColor = palette.surface;
	look.rowBandColor = rgb(0x1e1e1e);
	look.rowStripColor = rgb(0x131313);
	look.uppercaseHeader = true;
	return theme;
}

// Ruled paper: a blue hairline under every row, every second row tinted, and a red margin line
[[nodiscard]] Theme notebookTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0xf3f6fb), .surface = rgb(0xffffff), .surfaceAlt = rgb(0xe8f1fb),
		.text = rgb(0x1b2a5c), .textDim = rgb(0x5a6788),
		.button = rgb(0xffffff),
		.accent = rgb(0x2447b8),
		.selectionBg = rgb(0xfde3e3), .selectionFg = rgb(0xc62828),
		.border = rgb(0xc9d5e8),
	};

	return {
		.name = QStringLiteral("Notebook"),
		.dark = false,
		.palette = palette,
		.metrics = {
			.controlRadius = 4, .panelRadius = 6, .menuItemRadius = 3,
		},
		.fileList = {
			.rowFillInset = 0, .rowFillGap = 0,
			.rowFillRadius = 0,
			.contentHMargin = 8, .contentVMargin = 4,
			.rowSeparatorColor = rgb(0xb9d2f0),
			.rowBandColor = rgb(0xeef5fc),
			.marginLineColor = rgb(0xf2a7a7),
		},
	};
}

// The menu bar is plain text on the window's background. The column headers are a band.
// The function keys' padding leaves room for the key caps' glow.
// The splitter handles are transparent: the panels' glow, which the window paints, shows through them.
constexpr char kArcadeStyleSheet[] = R"(
	QSplitter::handle { background: transparent; }
	QMenuBar { background: transparent; color: @text@; }
	QMenuBar::item { color: @text@; }
	QMenuBar::item:selected, QMenuBar::item:pressed { background: @accentBg@; color: @accentText@; }

	CFileListView QHeaderView::section { background: @surfaceAlt@; color: @text@; }

	#CMainWindow > #centralwidget > #_commandLinePrompt { color: @cursor@; }
	CFunctionKeyButton { padding: 5px 8px; }
)";

// Text mode in arcade neon: lit double-line frames with the tabs standing on them, a pink cursor bar, lavender names on near-black violet.
// The panels are darker than the aubergine bezel around them.
[[nodiscard]] Theme arcadeTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0x1a1030), .surface = rgb(0x0f0a1c), .surfaceAlt = rgb(0x382458),
		.text = rgb(0xcbc1f0), .textDim = rgb(0x9285c6),
		.button = rgb(0x2b1b4a),
		.accent = rgb(0x7cf3c8),
		.selectionBg = rgb(0x3f1f52), .selectionFg = rgb(0xff8ec4),
		.border = rgb(0x4f7c83), .borderSubtle = rgb(0x3a2a60), .borderStrong = rgb(0x5e4e8c),
	};

	Theme theme = flatMonospaceTheme(QStringLiteral("Arcade"), true, palette);
	theme.panelLayout = PanelLayout::TabsOnFrame;
	theme.pathBar = PathBarStyle::Titled;
	theme.panelFrame = PanelFrame::Double;
	theme.functionKeys = FunctionKeyForm::KeyCap;
	theme.menuBarMnemonicColor = palette.accent;
	theme.commandLineBlockCursor = true;
	theme.styleSheet = kArcadeStyleSheet;

	ThemeMetrics& metrics = theme.metrics;
	metrics.panelPadding = 8;
	metrics.windowMargin = 9;
	metrics.panelGap = 8;
	metrics.commandLineMargin = 12;
	metrics.functionKeySpacing = 16;
	metrics.functionKeyBarMargin = 8;
	metrics.tabSpacing = 4;
	metrics.pathTitleScale = 1.0;
	metrics.glowRadius = 9;

	FileListLook& look = theme.fileList;
	look.contentVMargin = 2;
	look.cursorColor = rgb(0xff5fa8);
	theme.summaryValueColor = look.cursorColor;
	look.extensionInNameColor = true;
	look.uppercaseHeader = true;
	return theme;
}

// The active panel has an accent border, the other none.
// The bookmarks and root buttons are circles: their width is fixed in the .ui file, and min-height excludes the padding.
constexpr char kMeadowStyleSheet[] = R"(
	CPanelWidget { border-color: transparent; }
	CPanelWidget[activePanel="true"] { border-color: @accent@; }

	QTabBar::tab, CTabStrip > QPushButton { border-bottom: none; border-radius: @controlRadius@px; padding: 4px 12px; }
	QTabBar::tab:selected, CTabStrip > QPushButton:checked { background: @surfaceAlt@; }

	QHeaderView::section { border-bottom: none; font-weight: bold; }

	CPanelWidget > QToolButton { background: @windowBg@; border: none; border-radius: 14px; padding: 0; min-height: 28px; max-height: 28px; }
	CPanelWidget > QToolButton:hover { background: @surfaceAlt@; }

	#_driveButtonsWidget > QPushButton { background: @windowBg@; font-weight: bold; }
	#_driveButtonsWidget > QPushButton:checked { background: @textDim@; color: @surface@; }
	CPanelWidget[activePanel="true"] #_driveButtonsWidget > QPushButton:checked { background: @accent@; color: @accentFg@; }
)";

[[nodiscard]] Theme meadowTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0xedf2ed), .surface = rgb(0xffffff), .surfaceAlt = rgb(0xe3ece4),
		.text = rgb(0x1c2a22), .textDim = rgb(0x53655a),
		.button = rgb(0xffffff),
		.accent = rgb(0x0f766e),
		.selectionBg = rgb(0xd4e6e5), .selectionFg = rgb(0x0d645e),
		.borderSubtle = rgb(0xdfe8e0),
	};

	return {
		.name = QStringLiteral("Meadow"),
		.dark = false,
		.monospaceControls = true,
		.palette = palette,
		.metrics = {
			.controlRadius = 11, .panelRadius = 14, .panelBorderWidth = 2, .menuItemRadius = 11,
			.panelPadding = 10,
			.windowMargin = 12, .panelGap = 10, .commandLineMargin = 12,
			.driveButtonSpacing = 4, // the drive buttons are filled chips
		},
		.fileList = {
			.rowFillInset = 8, .rowFillGap = 2,
			.rowFillRadius = 9,
			.contentHMargin = 10, .contentVMargin = 5,
			.cursor = FileListLook::Cursor::Bar,
			.inactiveCursor = FileListLook::InactiveCursor::Fill,
			.inactiveCursorColor = palette.surfaceAlt,
		},
		.styleSheet = kMeadowStyleSheet,
	};
}

// Panels edge to edge, one line apart, without frames; the active one has a lighter surface.
// The active panel's selected tab has an accent line on top.
constexpr char kDuskStyleSheet[] = R"(
	QMenuBar { border-bottom: 1px solid @border@; }
	QSplitter::handle { background: @border@; }

	QTabBar::tab, CTabStrip > QPushButton { border-bottom: none; border-top: 2px solid transparent; border-radius: 0; }
	QTabBar::tab:selected, CTabStrip > QPushButton:checked { background: @surfaceAlt@; }
	CPanelWidget[activePanel="true"] CTabStrip > QPushButton:checked { border-top-color: @accent@; }

	#_driveButtonsWidget > QPushButton:checked { background: @borderStrong@; color: @text@; }
	#_infoLabel { border-top: 1px solid @border@; padding-top: 6px; }
)";

[[nodiscard]] Theme duskTheme()
{
	const CBasePalette palette{
		.windowBg = rgb(0x1a1524), .surface = rgb(0x1c1728), .surfaceAlt = rgb(0x2f2643),
		.text = rgb(0xece6f5), .textDim = rgb(0xa99fc0),
		.button = rgb(0x241d33),
		.accent = rgb(0xf2b84b),
		.selectionBg = rgb(0x45362f), .selectionFg = rgb(0xf2b84b),
		.border = rgb(0x362c4a), .borderStrong = rgb(0x43375c),
	};

	return {
		.name = QStringLiteral("Dusk"),
		.dark = true,
		.palette = palette,
		.metrics = {
			.controlRadius = 6, .panelRadius = 0, .panelBorderWidth = 0, .menuItemRadius = 4,
			.windowMargin = 0, .panelGap = 1, .commandLineMargin = 10,
		},
		.fileList = {
			.rowFillInset = 0, .rowFillGap = 0,
			.rowFillRadius = 0,
			.contentHMargin = 12, .contentVMargin = 4,
			.cursor = FileListLook::Cursor::Bar,
			.inactiveCursorColor = ColorUtils::mix(palette.surface, palette.accent, 0.6f),
			.activeBackground = rgb(0x281f39),
			.rowBandColor = rgb(0x30264a),
			.rowsPerBand = 2,
			.rowBandsInActivePanelOnly = true,
		},
		.styleSheet = kDuskStyleSheet,
	};
}

// No boxes: panels edge to edge, one line apart. Underlines mark the command line and the selected drive and tab.
constexpr char kLedgerStyleSheet[] = R"(
	QMenuBar { border-bottom: 1px solid @border@; }
	QSplitter::handle { background: @border@; }

	QTabBar::tab:selected, CTabStrip > QPushButton:checked { border-bottom-color: @text@; }
	QHeaderView::section { font-weight: bold; border-bottom: 2px solid @text@; }

	CPanelWidget > QToolButton { border-color: transparent; }

	#_driveButtonsWidget > QPushButton { border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 2px 6px; }
	#_driveButtonsWidget > QPushButton:hover { background: transparent; border-bottom-color: @textDim@; }
	#_driveButtonsWidget > QPushButton:checked { background: transparent; border-bottom-color: @text@; color: @text@; font-weight: bold; }

	#_infoLabel { border-top: 2px solid @text@; padding-top: 6px; }
	#CMainWindow > #centralwidget > QComboBox { background: transparent; border: none; border-bottom: 1px solid @text@; border-radius: 0; }
)";

// Ledger in one polarity: everything but the palette is shared.
// The row hairlines take the palette's borderSubtle, which must be authored: the palette is not resolved yet.
[[nodiscard]] Theme ledgerTheme(const bool dark, const CBasePalette& palette)
{
	return {
		.name = QStringLiteral("Ledger"),
		.dark = dark,
		.pathBar = PathBarStyle::Titled,
		.palette = palette,
		.metrics = {
			.controlRadius = 3, .panelRadius = 0, .panelBorderWidth = 0, .menuItemRadius = 3,
			.panelPadding = 14,
			.windowMargin = 0, .panelGap = 1, .commandLineMargin = 16,
		},
		.fileList = {
			.rowFillInset = 0, .rowFillGap = 0,
			.rowFillRadius = 0,
			.contentHMargin = 6, .contentVMargin = 6,
			.cursor = FileListLook::Cursor::Bar,
			.inactiveCursorColor = palette.accent,
			.rowSeparatorColor = palette.borderSubtle,
			.rowsPerBand = 5,
			.rowBandSeparatorColor = ColorUtils::mix(palette.surface, palette.text, 0.3f),
			.rowBandSeparatorWidth = 2,
			.uppercaseHeader = true,
		},
		.styleSheet = kLedgerStyleSheet,
	};
}

void applySelectedTheme()
{
	g_appliedTheme = CThemeController::instance().selectedTheme(allThemes());
	g_appliedTheme.palette = resolvedPalette(g_appliedTheme.palette);

	QFont font = *g_baseFont;
	if (g_appliedTheme.monospaceControls)
		font.setFamily(monospaceFamily());
	if (const std::optional<int> size = tabletModeFontPointSize())
		font.setPointSize(*size);
	if (QApplication::font() != font)
		QApplication::setFont(font);

	QApplication::setPalette(qtPaletteFor(g_appliedTheme.palette));
	qApp->setStyleSheet(buildStyleSheet(g_appliedTheme));
}

} // namespace

const std::vector<Theme>& allThemes()
{
	static const std::vector<Theme> themes{
		{
			.name = QStringLiteral("Cobalt"),
			.dark = true,
			.palette = {
				.windowBg = rgb(0x14161a), .surface = rgb(0x1d2026), .surfaceAlt = rgb(0x22262d),
				.text = rgb(0xe4e7ec), .textDim = rgb(0x9aa3b2),
				.button = rgb(0x262a32),
				.accent = rgb(0x5b9dff),
				.selectionBg = rgb(0x2e4363), .selectionFg = rgb(0xf2f5fa),
				.border = rgb(0x2c313a),
			},
		},
		// Listed before Cobalt's light variant: the first theme of a polarity is its default.
		meadowTheme(),
		{
			.name = QStringLiteral("Cobalt"),
			.dark = false,
			.palette = {
				.windowBg = rgb(0xeef0f4), .surface = rgb(0xffffff), .surfaceAlt = rgb(0xf5f6f9),
				.text = rgb(0x171a20), .textDim = rgb(0x5b6472),
				.button = rgb(0xf8f9fb),
				.accent = rgb(0x1f5fd1),
				.selectionBg = rgb(0xd7e2f7), .selectionFg = rgb(0x171a20),
				.border = rgb(0xd5d9e0),
			},
		},
		blueprintTheme(true, {
			.windowBg = rgb(0x0a2158), .surface = rgb(0x0a2158), .surfaceAlt = rgb(0x16346f),
			.text = rgb(0xd6e6ff), .textDim = rgb(0x9cbcf0),
			.button = rgb(0x0a2158),
			.accent = rgb(0x45d6e6),
			.selectionBg = rgb(0x19507d), .selectionFg = rgb(0xffd866),
			.border = rgb(0x4f76c8), .borderSubtle = rgb(0x2f55a8), .borderStrong = rgb(0x5f8be0),
			.accentFg = rgb(0x06204f),
		}),
		blueprintTheme(false, {
			.windowBg = rgb(0xeef4ec), .surface = rgb(0xeef4ec), .surfaceAlt = rgb(0xdfeadb),
			.text = rgb(0x17324d), .textDim = rgb(0x52685e),
			.button = rgb(0xeef4ec),
			.accent = rgb(0x1e5aa8),
			.selectionBg = rgb(0xfbe3c8), .selectionFg = rgb(0xa3470f),
			.border = rgb(0x9cc0a1), .borderSubtle = rgb(0xc9dec8), .borderStrong = rgb(0x6f9a78),
			.accentFg = rgb(0xffffff),
		}),
		ledgerTheme(true, {
			.windowBg = rgb(0x1a1916), .surface = rgb(0x1a1916), .surfaceAlt = rgb(0x24221e),
			.text = rgb(0xece7dd), .textDim = rgb(0x9a9488),
			.button = rgb(0x1a1916),
			.accent = rgb(0xe9a752),
			.selectionBg = rgb(0x3a2f20), .selectionFg = rgb(0xf2bd74),
			.border = rgb(0x36332d), .borderSubtle = rgb(0x2a2824),
		}),
		ledgerTheme(false, {
			.windowBg = rgb(0xffffff), .surface = rgb(0xffffff), .surfaceAlt = rgb(0xf3f5f8),
			.text = rgb(0x161a22), .textDim = rgb(0x5a6372),
			.button = rgb(0xffffff),
			.accent = rgb(0x1d4ed8),
			.selectionBg = rgb(0xedf1fc), .selectionFg = rgb(0x1d4ed8),
			.border = rgb(0xd9dde5), .borderSubtle = rgb(0xe6e9ee),
		}),
		duskTheme(),
		greenbarTheme(),
		departuresTheme(),
		notebookTheme(),
		arcadeTheme(),
	};
	return themes;
}

void installTheme(QApplication& app)
{
	// The themes are drawn for Fusion: the platform styles ignore the palette in places.
	// The proxy hides the native cursor of a line edit that draws a block cursor.
	QStyle* const fusion = QStyleFactory::create(QStringLiteral("Fusion"));
	assert_r(fusion);
	QApplication::setStyle(new CTextCursorHidingStyle{ fusion });

	// Serves the stylesheet's tinted glyphs. Must exist before a sheet refers to one, and for the application's lifetime.
	static const CThemeIconHandler iconHandler{ QStringLiteral(":/theme") };

	// One point larger than the system's font
	g_baseFont = QApplication::font();
	g_baseFont->setPointSizeF(g_baseFont->pointSizeF() + 1.0);

	QObject::connect(&CThemeController::instance(), &CThemeController::themeChanged, &app, &applySelectedTheme);
	applySelectedTheme();
}

const Theme& appliedTheme()
{
	assert_r(!g_appliedTheme.name.isEmpty());
	return g_appliedTheme;
}

void setTabletMode(const bool enabled)
{
	if (g_tabletMode == enabled)
		return;

	g_tabletMode = enabled;
	applySelectedTheme();
}

std::optional<int> tabletModeFontPointSize()
{
	return g_tabletMode ? std::optional{ tabletFontPointSize } : std::nullopt;
}

QFont fileListFont()
{
	assert_and_return_r(g_baseFont, {});

	QFont font;
	if (font.fromString(QSettings{}.value(KEY_INTERFACE_FILE_LIST_FONT).toString()) && QFontDatabase::hasFamily(font.family()))
		return font;

	font = QFont{ monospaceFamily() };
	font.setPointSizeF(g_baseFont->pointSizeF());
	return font;
}

#include "theme.h"
#include "stylesheet.h"

#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "theme/colorutils.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLatin1String>
#include <QPalette>
#include <QSettings>
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

// Blueprint in one polarity: everything but the palette is shared. The column lines take the palette's borderSubtle.
[[nodiscard]] Theme blueprintTheme(const bool dark, const CBasePalette& palette)
{
	return {
		.name = QStringLiteral("Blueprint"),
		.dark = dark,
		.structure = ThemeStructure::Flat,
		.monospaceControls = true,
		.pathBar = PathBarStyle::Strip,
		.palette = palette,
		.metrics = {
			.controlRadius = 0, .panelRadius = 0, .menuItemRadius = 0,
			.panelPadding = 10,
			.windowMargin = 10, .panelGap = 10, .commandLineMargin = 10,
			.functionKeySpacing = 0, .functionKeyBarMargin = 0,
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

// The active panel has an accent border, the other none.
// The bookmarks and root buttons are circles: their width is fixed in the .ui file, and min-height excludes the padding.
constexpr char kMeadowStyleSheet[] = R"(
	CPanelWidget { border-color: transparent; }
	CPanelWidget[activePanel="true"] { border-color: @accent@; }

	QTabBar::tab { border-bottom: none; border-radius: @controlRadius@px; padding: 4px 12px; }
	QTabBar::tab:selected { background: @surfaceAlt@; }

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

	QTabBar::tab { border-bottom: none; border-top: 2px solid transparent; border-radius: 0; }
	QTabBar::tab:selected { background: @surfaceAlt@; }
	CPanelWidget[activePanel="true"] QTabBar::tab:selected { border-top-color: @accent@; }

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
		},
		.styleSheet = kDuskStyleSheet,
	};
}

// No boxes: panels edge to edge, one line apart. Underlines mark the command line and the selected drive and tab.
constexpr char kLedgerStyleSheet[] = R"(
	QMenuBar { border-bottom: 1px solid @border@; }
	QSplitter::handle { background: @border@; }

	QTabBar::tab:selected { border-bottom-color: @text@; }
	QHeaderView::section { font-weight: bold; border-bottom: 2px solid @text@; }

	CPanelWidget > QToolButton { border-color: transparent; }

	#_driveButtonsWidget > QPushButton { border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 2px 6px; }
	#_driveButtonsWidget > QPushButton:hover { background: transparent; border-bottom-color: @textDim@; }
	#_driveButtonsWidget > QPushButton:checked { background: transparent; border-bottom-color: @text@; color: @text@; font-weight: bold; }

	#_infoLabel { border-top: 2px solid @text@; padding-top: 6px; }
	CMainWindow > #centralwidget > QComboBox { background: transparent; border: none; border-bottom: 1px solid @text@; border-radius: 0; }
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
		meadowTheme(),
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
	};
	return themes;
}

void installTheme(QApplication& app)
{
	// The themes are drawn for Fusion: the platform styles ignore the palette in places
	assert_r(QApplication::setStyle(QStringLiteral("Fusion")));

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

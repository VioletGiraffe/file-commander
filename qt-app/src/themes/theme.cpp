#include "theme.h"
#include "stylesheet.h"

#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
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
		.palette = palette,
		.metrics = {
			.controlRadius = 0, .panelRadius = 0, .menuItemRadius = 0,
			.panelPadding = 10,
			.windowMargin = 10, .panelGap = 10,
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
			.windowMargin = 12, .panelGap = 10,
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
			.windowBg = rgb(0xeef3fb), .surface = rgb(0xeef3fb), .surfaceAlt = rgb(0xdfe8f6),
			.text = rgb(0x0a2158), .textDim = rgb(0x4a6396),
			.button = rgb(0xeef3fb),
			.accent = rgb(0x0a7486),
			.selectionBg = rgb(0xc0dae4), .selectionFg = rgb(0xa01840),
			.border = rgb(0x7f9bd1), .borderSubtle = rgb(0xc3d1ea), .borderStrong = rgb(0x5f7fc0),
			.accentFg = rgb(0xffffff),
		}),
		meadowTheme(),
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

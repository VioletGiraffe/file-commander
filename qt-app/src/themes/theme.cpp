#include "theme.h"
#include "stylesheet.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "theme/cthemecontroller.h"
#include "theme/cthemeiconhandler.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QPalette>
RESTORE_COMPILER_WARNINGS

namespace {

QColor rgb(QRgb value) { return QColor::fromRgb(value); }

CBasePalette g_appliedPalette;

void applySelectedTheme()
{
	Theme theme = CThemeController::instance().selectedTheme(allThemes());
	theme.palette = resolvedPalette(theme.palette);
	g_appliedPalette = theme.palette;

	QApplication::setPalette(qtPaletteFor(theme.palette));
	qApp->setStyleSheet(buildStyleSheet(theme));
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
	};
	return themes;
}

void installTheme(QApplication& app)
{
	// The themes are drawn for Fusion: the platform styles ignore the palette in places
	assert_r(QApplication::setStyle(QStringLiteral("Fusion")));

	// Serves the stylesheet's tinted glyphs. Must exist before a sheet refers to one, and for the application's lifetime.
	static const CThemeIconHandler iconHandler{ QStringLiteral(":/theme") };

	QObject::connect(&CThemeController::instance(), &CThemeController::themeChanged, &app, &applySelectedTheme);
	applySelectedTheme();
}

const CBasePalette& appliedThemePalette()
{
	assert_r(g_appliedPalette.text.isValid());
	return g_appliedPalette;
}

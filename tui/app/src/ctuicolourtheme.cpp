#include "ctuicolourtheme.h"

#include <algorithm>

namespace {

// Colours are in the xterm 256-colour cube or grey ramp: a 256-colour terminal shows them exactly, and no terminal colour scheme redefines them.
// The exceptions are marked "off-palette", or the whole theme "true colour": a 256-colour terminal shows the nearest colour in the palette.
constexpr TColorRGB black{ 0x000000 }, navy{ 0x00005F }, mediumBlue{ 0x0000AF }, red{ 0xAF0000 }, amber{ 0xD78700 }, cyan{ 0x00AFAF }, teal{ 0x008787 },
	darkTeal{ 0x005F5F }, darkGrey{ 0x5F5F5F }, grey{ 0xAFAFAF }, lightGreen{ 0x5FFF5F }, lightCyan{ 0x5FFFFF }, mutedCyan{ 0x5FD7D7 }, yellow{ 0xFFFF5F }, white{ 0xFFFFFF };

constexpr ColourTheme themes[]{
	{ "classic-blue", "Classic blue", ColourTheme::Polarity::Dark, {
		.menuBack = grey, .menuText = black, .menuShortcut = red, .menuSelectedBack = amber, .menuSelectedText = black, .menuSelectedShortcut = navy,
		.disabledText = darkGrey,
		.panelBack = navy, .framePassive = grey, .frameActive = white, .frameIcons = lightGreen,
		.fileText = lightCyan, .fileDetailText = mutedCyan, .selectedText = yellow, .cursorBack = teal, .cursorText = black, .cursorSelectedText = yellow, .columnHeader = white,
		.statusText = grey, .separator = mediumBlue, .cursorSeparator = darkTeal, .tabBack = cyan, .tabText = black,
		.dialogBack = grey, .dialogText = black, .dialogFrame = white, .dialogShortcut = yellow, .focusText = white,
		.buttonBack = amber, .buttonText = black, .buttonDefaultText = navy, .buttonShortcut = yellow, .buttonShadow = black,
		.controlBack = cyan, .controlText = black, .inputBack = navy, .inputText = white, .scrollBarText = cyan, .scrollBarBack = navy, .selectionText = white,
		.bottomBack = black, .hintText = grey, .hintKey = cyan, .keyBack = yellow, .keyText = black, .keyLabelBack = grey, .keyLabelText = black,
		.keyBarBack = black } },
	{ "dark", "Dark", ColourTheme::Polarity::Dark, {
		.menuBack = 0x303030, .menuText = 0xD0D0D0, .menuShortcut = 0xFFAF00, .menuSelectedBack = 0x005F87, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD75F,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x121212, .rowBandBack = 0x1C1C1C, .rowsPerBand = 1, .framePassive = 0x585858, .frameActive = 0xD0D0D0, .frameIcons = 0x5FD75F,
		.fileText = 0xBCBCBC, .fileDetailText = 0x949494, .selectedText = 0xFFD75F, .cursorBack = 0x005F87, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFD75F, .columnHeader = 0x5FAFD7,
		.statusText = 0x8A8A8A, .separator = 0x303030, .cursorSeparator = 0x5F87AF, .tabBack = 0x4E4E4E, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFFAF00, .focusText = 0xFFFFFF,
		.buttonBack = 0x4E4E4E, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD75F, .buttonShortcut = 0xFFAF00, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0, .inputBack = 0x121212, .inputText = 0xFFFFFF, .scrollBarText = 0x585858, .scrollBarBack = 0x1C1C1C,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x000000, .hintText = 0x8A8A8A, .hintKey = 0xFFAF00, .keyBack = 0xFFD75F, .keyText = 0x000000, .keyLabelBack = 0x005F87, .keyLabelText = 0xFFFFFF,
		.keyBarBack = 0x000000 } },
	{ "amber", "Amber", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3A3A3A, .menuText = 0xFFAF00, .menuShortcut = 0xFFFFFF, .menuSelectedBack = 0xD78700, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0xFFFFFF,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x121212, .framePassive = 0x875F00, .frameActive = 0xFFAF00, .frameIcons = 0xFFD787,
		.fileText = 0xD78700, .fileDetailText = 0xD78700, .selectedText = 0xFFFFFF, .cursorBack = 0x875F00, .cursorText = 0xFFD787, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xFFD787,
		.statusText = 0xAF8700, .separator = 0x3A3A3A, .cursorSeparator = 0xAF8700, .tabBack = 0x875F00, .tabText = 0xFFD787,
		.dialogBack = 0x3A3A3A, .dialogText = 0xFFAF00, .dialogFrame = 0xFFD787, .dialogShortcut = 0xFFFFFF, .focusText = 0xFFFFFF,
		.buttonBack = 0x875F00, .buttonText = 0xFFD787, .buttonDefaultText = 0xFFFFFF, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x000000,
		.controlBack = 0x262626, .controlText = 0xFFAF00, .inputBack = 0x121212, .inputText = 0xFFD787, .scrollBarText = 0x875F00, .scrollBarBack = 0x1C1C1C,
		.selectionText = 0x000000,
		.bottomBack = 0x000000, .hintText = 0xAF8700, .hintKey = 0xFFD787, .keyBack = 0xFFAF00, .keyText = 0x000000, .keyLabelBack = 0x3A3A3A, .keyLabelText = 0xFFAF00,
		.keyBarBack = 0x000000 } },
	{ "purple-night", "Purple night", ColourTheme::Polarity::Dark, {
		.menuBack = 0x303030, .menuText = 0xD0D0D0, .menuShortcut = 0xFF87D7, .menuSelectedBack = 0x875FD7, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD7FF,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x1C1C1C, .framePassive = 0x5F5F87, .frameActive = 0xD0D0D0, .frameIcons = 0xAF87FF,
		.fileText = 0xD0D0D0, .fileDetailText = 0xA8A8A8, .selectedText = 0xFF87D7, .cursorBack = 0x5F5F87, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFAFD7, .columnHeader = 0xAF87FF,
		.statusText = 0x8A8A8A, .separator = 0x303030, .cursorSeparator = 0x8787AF, .tabBack = 0x5F5F87, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFF87D7, .focusText = 0xFFFFFF,
		.buttonBack = 0x5F5F87, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD7FF, .buttonShortcut = 0xFF87D7, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0, .inputBack = 0x1C1C1C, .inputText = 0xFFFFFF, .scrollBarText = 0x5F5F87, .scrollBarBack = 0x262626,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x000000, .hintText = 0x8A8A8A, .hintKey = 0xAF87FF, .keyBack = 0xFF87D7, .keyText = 0x000000, .keyLabelBack = 0x5F5F87, .keyLabelText = 0xFFFFFF,
		.keyBarBack = 0x000000 } },
	{ "deep-teal", "Deep teal", ColourTheme::Polarity::Dark, {
		.menuBack = 0x008787, .menuText = 0xFFFFFF, .menuShortcut = 0xFFD75F, .menuSelectedBack = 0x00AFAF, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0x00005F,
		.disabledText = 0x87AFAF,
		.panelBack = 0x002828, // Off-palette
		.framePassive = 0x5F8787, .frameActive = 0xFFFFFF, .frameIcons = 0x5FD75F,
		.fileText = 0xAFD7D7, .fileDetailText = 0x87AFAF, .selectedText = 0xFFD75F, .cursorBack = 0x00AFAF, .cursorText = 0x000000, .cursorSelectedText = 0xFFFF5F, .columnHeader = 0xFFFFFF,
		.statusText = 0x87AFAF,
		.separator = 0x004A4A, // Off-palette
		.cursorSeparator = 0x008787, .tabBack = 0x008787, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFFD75F, .focusText = 0xFFFFFF,
		.buttonBack = 0x008787, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFFF5F, .buttonShortcut = 0xFFD75F, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0,
		.inputBack = 0x002828, // Off-palette
		.inputText = 0xFFFFFF, .scrollBarText = 0x5F8787, .scrollBarBack = 0x1C1C1C, .selectionText = 0x000000,
		.bottomBack = 0x000000, .hintText = 0x87AFAF, .hintKey = 0x00AFAF, .keyBack = 0x00AFAF, .keyText = 0x000000, .keyLabelBack = 0x005F5F, .keyLabelText = 0xFFFFFF,
		.keyBarBack = 0x000000 } },
	// True colour, built on the films' code-rain greens, with red accents
	{ "matrix", "Matrix", ColourTheme::Polarity::Dark, {
		.menuBack = 0x0D2614, .menuText = 0xA8F0BC, .menuShortcut = 0xFF4545, .menuSelectedBack = 0x00FF41, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0x8A0000,
		.disabledText = 0x3E6B4A,
		.panelBack = 0x030805, .framePassive = 0x1F5C2E, .frameActive = 0x00FF41, .frameIcons = 0xFF4545,
		.fileText = 0x00D33A, .fileDetailText = 0x0EA230, .selectedText = 0xFFFFFF, .cursorBack = 0x00E03A, .cursorText = 0x021006, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xCFFFD9,
		.statusText = 0x5FAF74, .separator = 0x143D1F, .cursorSeparator = 0x007A20, .tabBack = 0x0D2614, .tabText = 0xFF7070,
		.dialogBack = 0x0D2614, .dialogText = 0xC4F5D0, .dialogFrame = 0x00FF41, .dialogShortcut = 0xFF5A5A, .focusText = 0xFFFFFF,
		.buttonBack = 0x1F5C2E, .buttonText = 0xE4FFEA, .buttonDefaultText = 0xFFFFFF, .buttonShortcut = 0xFF7070, .buttonShadow = 0x000000,
		.controlBack = 0x163D21, .controlText = 0xC4F5D0, .inputBack = 0x000000, .inputText = 0x00FF41, .scrollBarText = 0x1F5C2E, .scrollBarBack = 0x0D2614,
		.selectionText = 0x000000,
		.bottomBack = 0x030805, .hintText = 0x5FAF74, .hintKey = 0xFF5A5A, .keyBack = 0x00FF41, .keyText = 0x000000, .keyLabelBack = 0x0D2614, .keyLabelText = 0xA8F0BC,
		.keyBarBack = 0x030805 } },
	// True colour, in the yellow-green of P1 monitor phosphor, with the amber of P3
	{ "phosphor", "Phosphor", ColourTheme::Polarity::Dark, {
		.menuBack = 0x0F2600, .menuText = 0xC8FF9A, .menuShortcut = 0xFFB000, .menuSelectedBack = 0x5BFF1F, .menuSelectedText = 0x0A1400, .menuSelectedShortcut = 0x6B3A00,
		.disabledText = 0x2E5A10,
		.panelBack = 0x000000, .framePassive = 0x1E5200, .frameActive = 0x5BFF1F, .frameIcons = 0xFFB000,
		.fileText = 0x40D400, .fileDetailText = 0x2FA400, .selectedText = 0xFFC23A, .cursorBack = 0x5BFF1F, .cursorText = 0x0A1400, .cursorSelectedText = 0x7A2E00, .columnHeader = 0xF2FFE0,
		.statusText = 0x5B9A3C, .separator = 0x142E00, .cursorSeparator = 0x2E8F00, .tabBack = 0x0F2600, .tabText = 0xFFB000,
		.dialogBack = 0x0A1A00, .dialogText = 0xC8FF9A, .dialogFrame = 0x5BFF1F, .dialogShortcut = 0xFFB000, .focusText = 0xFFFFFF,
		.buttonBack = 0x1E5200, .buttonText = 0xF2FFE0, .buttonDefaultText = 0xFFFFFF, .buttonShortcut = 0xFFC23A, .buttonShadow = 0x000000,
		.controlBack = 0x0F2600, .controlText = 0xC8FF9A, .inputBack = 0x000000, .inputText = 0x9BFF6A, .scrollBarText = 0x1E5200, .scrollBarBack = 0x0A1A00,
		.selectionText = 0x0A1400,
		.bottomBack = 0x0A1A00, .hintText = 0x5B9A3C, .hintKey = 0xFFB000, .keyBack = 0xFFB000, .keyText = 0x1A0E00, .keyLabelBack = 0x1E5200, .keyLabelText = 0xC8FF9A,
		.keyBarBack = 0x000000 } },
	// True colour, in an aqua phosphor green
	{ "vector", "Vector", ColourTheme::Polarity::Dark, {
		.menuBack = 0x000000, .menuText = 0x19D99A, .menuShortcut = 0xB5FFE6, .menuSelectedBack = 0x00FFB0, .menuSelectedText = 0x00140E, .menuSelectedShortcut = 0x0B4A38,
		.disabledText = 0x0E5A44,
		.panelBack = 0x000000, .framePassive = 0x0E5A44, .frameActive = 0x00FFB0, .frameIcons = 0xB5FFE6,
		.fileText = 0x14B884, .fileDetailText = 0x0C8C63, .selectedText = 0xECFFF8, .cursorBack = 0x063A2C, .cursorText = 0x4DFFC4, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xB5FFE6,
		.statusText = 0x13906A, .separator = 0x061A15, .cursorSeparator = 0x0E5A44, .tabBack = 0x063A2C, .tabText = 0x4DFFC4,
		.dialogBack = 0x000000, .dialogText = 0x19D99A, .dialogFrame = 0x00FFB0, .dialogShortcut = 0xB5FFE6, .focusText = 0xECFFF8,
		.buttonBack = 0x04241C, .buttonText = 0x4DFFC4, .buttonDefaultText = 0xECFFF8, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x04140F,
		.controlBack = 0x04140F, .controlText = 0x19D99A, .inputBack = 0x04140F, .inputText = 0x4DFFC4, .scrollBarText = 0x0E5A44, .scrollBarBack = 0x000000,
		.selectionText = 0x00140E,
		.bottomBack = 0x000000, .hintText = 0x13906A, .hintKey = 0x4DFFC4, .keyBack = 0x063A2C, .keyText = 0x4DFFC4, .keyLabelBack = 0x000000, .keyLabelText = 0x19D99A,
		.keyBarBack = 0x000000 } },
	// True colour, mostly from the Nord palette (MIT licence)
	{ "slate", "Slate", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3B4252, .menuText = 0xD8DEE9, .menuShortcut = 0x88C0D0, .menuSelectedBack = 0x5E81AC, .menuSelectedText = 0xECEFF4, .menuSelectedShortcut = 0xEBCB8B,
		.disabledText = 0x616E88,
		.panelBack = 0x2E3440, .framePassive = 0x4C566A, .frameActive = 0xD8DEE9, .frameIcons = 0xA3BE8C,
		.fileText = 0xD8DEE9, .fileDetailText = 0xB7BEC9, .selectedText = 0xEBCB8B, .cursorBack = 0x5E81AC, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFE4A8, .columnHeader = 0x81A1C1,
		.statusText = 0x7B88A1, .separator = 0x3B4252, .cursorSeparator = 0x81A1C1, .tabBack = 0x4C566A, .tabText = 0xECEFF4,
		.dialogBack = 0x3B4252, .dialogText = 0xD8DEE9, .dialogFrame = 0xECEFF4, .dialogShortcut = 0x88C0D0, .focusText = 0x88C0D0,
		.buttonBack = 0x5A6480, .buttonText = 0xECEFF4, .buttonDefaultText = 0xEBCB8B, .buttonShortcut = 0x88C0D0, .buttonShadow = 0x242933,
		.controlBack = 0x434C5E, .controlText = 0xD8DEE9, .inputBack = 0x2E3440, .inputText = 0xECEFF4, .scrollBarText = 0x4C566A, .scrollBarBack = 0x2E3440,
		.selectionText = 0xECEFF4,
		.bottomBack = 0x242933, .hintText = 0x7B88A1, .hintKey = 0x88C0D0, .keyBack = 0x88C0D0, .keyText = 0x2E3440, .keyLabelBack = 0x3B4252, .keyLabelText = 0xD8DEE9,
		.keyBarBack = 0x242933 } },
	// True colour, mostly from the Gruvbox palette (MIT licence)
	{ "ember", "Ember", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3C3836, .menuText = 0xEBDBB2, .menuShortcut = 0xFE8019, .menuSelectedBack = 0xD65D0E, .menuSelectedText = 0xFBF1C7, .menuSelectedShortcut = 0xFABD2F,
		.disabledText = 0x7C6F64,
		.panelBack = 0x1D2021, .rowBandBack = 0x282828, .rowsPerBand = 3, .framePassive = 0x504945, .frameActive = 0xEBDBB2, .frameIcons = 0xB8BB26,
		.fileText = 0xD5C4A1, .fileDetailText = 0xA3987F, .selectedText = 0xFABD2F, .cursorBack = 0x504945, .cursorText = 0xFBF1C7, .cursorSelectedText = 0xFABD2F, .columnHeader = 0x83A598,
		.statusText = 0xA89984, .separator = 0x32302F, .cursorSeparator = 0x665C54, .tabBack = 0x504945, .tabText = 0xFBF1C7,
		.dialogBack = 0x3C3836, .dialogText = 0xEBDBB2, .dialogFrame = 0xFBF1C7, .dialogShortcut = 0xFE8019, .focusText = 0xFABD2F,
		.buttonBack = 0x504945, .buttonText = 0xFBF1C7, .buttonDefaultText = 0xFABD2F, .buttonShortcut = 0xFE8019, .buttonShadow = 0x141617,
		.controlBack = 0x32302F, .controlText = 0xEBDBB2, .inputBack = 0x1D2021, .inputText = 0xFBF1C7, .scrollBarText = 0x665C54, .scrollBarBack = 0x282828,
		.selectionText = 0xFBF1C7,
		.bottomBack = 0x141617, .hintText = 0xA89984, .hintKey = 0xFE8019, .keyBack = 0xFABD2F, .keyText = 0x1D2021, .keyLabelBack = 0x3C3836, .keyLabelText = 0xEBDBB2,
		.keyBarBack = 0x141617 } },
	// True colour, mostly from the Tokyo Night palette (MIT licence)
	{ "midnight", "Midnight", ColourTheme::Polarity::Dark, {
		.menuBack = 0x16161E, .menuText = 0xA9B1D6, .menuShortcut = 0xBB9AF7, .menuSelectedBack = 0x3D59A1, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xE0AF68,
		.disabledText = 0x545C7E,
		.panelBack = 0x1A1B26, .framePassive = 0x3B4261, .frameActive = 0xC0CAF5, .frameIcons = 0x9ECE6A,
		.fileText = 0xC0CAF5, .fileDetailText = 0x959DBF, .selectedText = 0xE0AF68, .cursorBack = 0x2F3B66, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFC777, .columnHeader = 0x7AA2F7,
		.statusText = 0x565F89, .separator = 0x24283B, .cursorSeparator = 0x3D59A1, .tabBack = 0x3B4261, .tabText = 0xFFFFFF,
		.dialogBack = 0x24283B, .dialogText = 0xC0CAF5, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xBB9AF7, .focusText = 0x7DCFFF,
		.buttonBack = 0x3B4261, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xE0AF68, .buttonShortcut = 0xBB9AF7, .buttonShadow = 0x101018,
		.controlBack = 0x292E42, .controlText = 0xC0CAF5, .inputBack = 0x1A1B26, .inputText = 0xFFFFFF, .scrollBarText = 0x3B4261, .scrollBarBack = 0x1A1B26,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x101018, .hintText = 0x565F89, .hintKey = 0xBB9AF7, .keyBack = 0x7AA2F7, .keyText = 0x16161E, .keyLabelBack = 0x24283B, .keyLabelText = 0xC0CAF5,
		.keyBarBack = 0x101018 } },
	// True colour, in the white of P4 monitor phosphor, banded like scan lines
	{ "scanline", "Scanline", ColourTheme::Polarity::Dark, {
		.menuBack = 0x111820, .menuText = 0xC9D6E8, .menuShortcut = 0x6FC3FF, .menuSelectedBack = 0xC9D6E8, .menuSelectedText = 0x05070A, .menuSelectedShortcut = 0x0B4F86,
		.disabledText = 0x4A5869,
		.panelBack = 0x05070A, .rowBandBack = 0x0D1219, .rowsPerBand = 1, .framePassive = 0x2A3646, .frameActive = 0xC9D6E8, .frameIcons = 0x6FC3FF,
		.fileText = 0xC9D6E8, .fileDetailText = 0x8496AD, .selectedText = 0xFFC857, .cursorBack = 0xC9D6E8, .cursorText = 0x05070A, .cursorSelectedText = 0x7A3E00, .columnHeader = 0xFFFFFF,
		.statusText = 0x6F8299, .separator = 0x1A2330, .cursorSeparator = 0x8496AD, .tabBack = 0x1A2330, .tabText = 0xFFFFFF,
		.dialogBack = 0x111820, .dialogText = 0xC9D6E8, .dialogFrame = 0xC9D6E8, .dialogShortcut = 0x6FC3FF, .focusText = 0xFFFFFF,
		.buttonBack = 0x2A3646, .buttonText = 0xF2F6FB, .buttonDefaultText = 0xFFC857, .buttonShortcut = 0x6FC3FF, .buttonShadow = 0x000000,
		.controlBack = 0x1A2330, .controlText = 0xC9D6E8, .inputBack = 0x000000, .inputText = 0xFFFFFF, .scrollBarText = 0x2A3646, .scrollBarBack = 0x0D1219,
		.selectionText = 0x05070A,
		.bottomBack = 0x05070A, .hintText = 0x6F8299, .hintKey = 0x6FC3FF, .keyBack = 0xC9D6E8, .keyText = 0x05070A, .keyLabelBack = 0x111820, .keyLabelText = 0xC9D6E8,
		.keyBarBack = 0x05070A } },
	// True colour, banded like the rows of a split-flap departure board
	{ "departures", "Departures", ColourTheme::Polarity::Dark, {
		.menuBack = 0x1F1F1F, .menuText = 0xE6E6E6, .menuShortcut = 0xFFD21F, .menuSelectedBack = 0xFFD21F, .menuSelectedText = 0x0A0A0A, .menuSelectedShortcut = 0xA3160C,
		.disabledText = 0x5A5A5A,
		.panelBack = 0x0A0A0A, .rowBandBack = 0x171717, .rowsPerBand = 1, .framePassive = 0x3A3A3A, .frameActive = 0xFFD21F, .frameIcons = 0xFF5A4D,
		.fileText = 0xFFD21F, .fileDetailText = 0xE6E6E6, .selectedText = 0x6EE7A0, .cursorBack = 0xFFD21F, .cursorText = 0x0A0A0A, .cursorSelectedText = 0x0B5A2A, .columnHeader = 0xFFFFFF,
		.statusText = 0x9A9A9A, .separator = 0x2A2A2A, .cursorSeparator = 0xB38F00, .tabBack = 0x2A2A2A, .tabText = 0xFFFFFF,
		.dialogBack = 0x1F1F1F, .dialogText = 0xE6E6E6, .dialogFrame = 0xFFD21F, .dialogShortcut = 0xFFD21F, .focusText = 0xFFFFFF,
		.buttonBack = 0x3A3A3A, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD21F, .buttonShortcut = 0xFFD21F, .buttonShadow = 0x000000,
		.controlBack = 0x2A2A2A, .controlText = 0xE6E6E6, .inputBack = 0x000000, .inputText = 0xFFD21F, .scrollBarText = 0x3A3A3A, .scrollBarBack = 0x171717,
		.selectionText = 0x0A0A0A,
		.bottomBack = 0x0A0A0A, .hintText = 0x9A9A9A, .hintKey = 0xFFD21F, .keyBack = 0xFFD21F, .keyText = 0x0A0A0A, .keyLabelBack = 0x1F1F1F, .keyLabelText = 0xE6E6E6,
		.keyBarBack = 0x0A0A0A } },
	// True colour, banded like rock strata
	{ "strata", "Strata", ColourTheme::Polarity::Dark, {
		.menuBack = 0x33291F, .menuText = 0xE0CDAA, .menuShortcut = 0x5FD6C4, .menuSelectedBack = 0xC9822B, .menuSelectedText = 0x1A0F04, .menuSelectedShortcut = 0x002B26,
		.disabledText = 0x6E5E4D,
		.panelBack = 0x1C1714, .rowBandBack = 0x28201A, .rowsPerBand = 2, .framePassive = 0x5A4737, .frameActive = 0xE0CDAA, .frameIcons = 0x5FD6C4,
		.fileText = 0xE0CDAA, .fileDetailText = 0xA8947A, .selectedText = 0x5FD6C4, .cursorBack = 0xC9822B, .cursorText = 0x1A0F04, .cursorSelectedText = 0x002B26, .columnHeader = 0xE8A85A,
		.statusText = 0x8C7A63, .separator = 0x3A2E25, .cursorSeparator = 0x8F5A1A, .tabBack = 0x5A4737, .tabText = 0xFFF3DC,
		.dialogBack = 0x33291F, .dialogText = 0xE0CDAA, .dialogFrame = 0xFFF3DC, .dialogShortcut = 0x5FD6C4, .focusText = 0xFFF3DC,
		.buttonBack = 0x5A4737, .buttonText = 0xFFF3DC, .buttonDefaultText = 0xFFC56E, .buttonShortcut = 0x7FE6D6, .buttonShadow = 0x120E0B,
		.controlBack = 0x28201A, .controlText = 0xE0CDAA, .inputBack = 0x120E0B, .inputText = 0xFFF3DC, .scrollBarText = 0x5A4737, .scrollBarBack = 0x28201A,
		.selectionText = 0x1A0F04,
		.bottomBack = 0x120E0B, .hintText = 0x8C7A63, .hintKey = 0xE8A85A, .keyBack = 0xC9822B, .keyText = 0x1A0F04, .keyLabelBack = 0x33291F, .keyLabelText = 0xE0CDAA,
		.keyBarBack = 0x120E0B } },
	// True colour, in arcade neon: mint frames, a pink cursor bar, lavender names on near-black violet, the menus and key buttons on a lighter aubergine bezel.
	// As the GUI's Arcade: the column header a band with uppercase titles, the status line's numbers in pink below a divider line.
	// The column header and the separators must round to 0x5F005F on a 256-colour terminal: darker purples round to navy, as dark as the panel's grey there.
	{ "arcade", "Arcade", ColourTheme::Polarity::Dark, {
		.menuBack = 0x1A1030, .menuText = 0xCBC1F0, .menuShortcut = 0x7CF3C8, .menuSelectedBack = 0xFF5FA8, .menuSelectedText = 0x0F0A1C, .menuSelectedShortcut = 0x00382B,
		.disabledText = 0x5E4E8C,
		.panelBack = 0x0F0A1C, .framePassive = 0x4F7C83, .frameActive = 0x7CF3C8, .frameIcons = 0xFF5FA8,
		.fileText = 0xCBC1F0, .fileDetailText = 0xA397D3, .selectedText = 0xFF8EC4, .cursorBack = 0xFF5FA8, .cursorText = 0x0F0A1C, .cursorSelectedText = 0x00382B, .columnHeader = 0x7CF3C8,
		.statusText = 0x9285C6, .separator = 0x3A2A60, .cursorSeparator = 0xC2457F,
		.columnHeaderBack = 0x382458, .uppercaseColumnHeader = true, .statusValue = 0xFF5FA8, .statusDividerLine = true, .statusDivider = 0x5E4E8C,
		.tabBack = 0x4A3878, .tabText = 0xE6E0FA,
		.dialogBack = 0x3A2862, .dialogText = 0xC9BFEA, .dialogFrame = 0x7CF3C8, .dialogShortcut = 0x7CF3C8, .focusText = 0xFF8EC4,
		.buttonBack = 0x4A3878, .buttonText = 0xFFFFFF, .buttonDefaultText = 0x7CF3C8, .buttonShortcut = 0xFF8EC4, .buttonShadow = 0x1A1030,
		.controlBack = 0x33225A, .controlText = 0xC9BFEA, .inputBack = 0x0F0A1C, .inputText = 0xFFFFFF, .scrollBarText = 0x5E4E8C, .scrollBarBack = 0x33225A,
		.selectionText = 0x0F0A1C,
		.bottomBack = 0x1A1030, .hintText = 0x9285C6, .hintKey = 0x7CF3C8, .keyBack = 0x7CF3C8, .keyText = 0x0F0A1C, .keyLabelBack = 0x3A2862, .keyLabelText = 0xCBC1F0,
		.keyBarBack = 0x1A1030 } },
	{ "two-inks", "Two inks", ColourTheme::Polarity::Light, {
		.menuBack = 0xFFFFFF, .menuText = 0x000000, .menuShortcut = 0xD70000, .menuSelectedBack = 0x000000, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFF5F5F,
		.disabledText = 0x8A8A8A,
		.panelBack = 0xFFFFFF, .framePassive = 0xBCBCBC, .frameActive = 0x000000, .frameIcons = 0xD70000,
		.fileText = 0x000000, .fileDetailText = 0x6C6C6C, .selectedText = 0xD70000, .cursorBack = 0x000000, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFF5F5F, .columnHeader = 0x000000,
		.statusText = 0x6C6C6C, .separator = 0xDADADA, .cursorSeparator = 0x6C6C6C, .tabBack = 0x000000, .tabText = 0xFFFFFF,
		.dialogBack = 0xEEEEEE, .dialogText = 0x000000, .dialogFrame = 0x000000, .dialogShortcut = 0xD70000, .focusText = 0xAF0000,
		.buttonBack = 0xD0D0D0, .buttonText = 0x000000, .buttonDefaultText = 0x5F0000, .buttonShortcut = 0xD70000, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xFFFFFF, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0x8A8A8A, .scrollBarBack = 0xE4E4E4,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xFFFFFF, .hintText = 0x6C6C6C, .hintKey = 0xD70000, .keyBack = 0x000000, .keyText = 0xFFFFFF, .keyLabelBack = 0xFFFFFF, .keyLabelText = 0x000000,
		.keyBarBack = 0xFFFFFF } },
	{ "highlighter", "Highlighter", ColourTheme::Polarity::Light, {
		.menuBack = 0xEEEEEE, .menuText = 0x000000, .menuShortcut = 0xD7005F, .menuSelectedBack = 0xFFFF5F, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0xD7005F,
		.disabledText = 0x8A8A8A,
		.panelBack = 0xFFFFFF, .framePassive = 0xBCBCBC, .frameActive = 0x000000, .frameIcons = 0x008700,
		.fileText = 0x1C1C1C, .fileDetailText = 0x6C6C6C, .selectedText = 0xD7005F, .cursorBack = 0xFFFF5F, .cursorText = 0x000000, .cursorSelectedText = 0xD7005F, .columnHeader = 0x005FD7,
		.statusText = 0x767676, .separator = 0xE4E4E4, .cursorSeparator = 0xD7D700, .tabBack = 0x87FF87, .tabText = 0x000000,
		.dialogBack = 0xEEEEEE, .dialogText = 0x000000, .dialogFrame = 0x000000, .dialogShortcut = 0xD7005F, .focusText = 0x0000D7,
		.buttonBack = 0xFFAFD7, .buttonText = 0x000000, .buttonDefaultText = 0x005F00, .buttonShortcut = 0x5F00AF, .buttonShadow = 0x9E9E9E,
		.controlBack = 0xFFFFFF, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0xBCBCBC, .scrollBarBack = 0xEEEEEE,
		.selectionText = 0x000000,
		.bottomBack = 0xFFFFFF, .hintText = 0x767676, .hintKey = 0x005FD7, .keyBack = 0x5FD7FF, .keyText = 0x000000, .keyLabelBack = 0xEEEEEE, .keyLabelText = 0x000000,
		.keyBarBack = 0xFFFFFF } },
	{ "light-cyan", "Light cyan", ColourTheme::Polarity::Light, {
		.menuBack = 0xAFD7D7, .menuText = 0x000000, .menuShortcut = 0xAF0000, .menuSelectedBack = 0x00005F, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFFF5F,
		.disabledText = 0x5F8787,
		.panelBack = 0xD7FFFF, .framePassive = 0x5F8787, .frameActive = 0x00005F, .frameIcons = 0x005F00,
		.fileText = 0x00005F, .fileDetailText = 0x000087, .selectedText = 0xAF0000, .cursorBack = 0x00AFAF, .cursorText = 0x000000, .cursorSelectedText = 0xFFFF5F, .columnHeader = 0x005FAF,
		.statusText = 0x005F5F, .separator = 0xAFD7D7, .cursorSeparator = 0x008787, .tabBack = 0x00AFAF, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x000000, .dialogFrame = 0x00005F, .dialogShortcut = 0xAF0000, .focusText = 0x00005F,
		.buttonBack = 0x00AFAF, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xAFD7D7, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x00005F, .scrollBarText = 0x5F8787, .scrollBarBack = 0xD7FFFF,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xAFD7D7, .hintText = 0x005F5F, .hintKey = 0xAF0000, .keyBack = 0xFFFF5F, .keyText = 0x000000, .keyLabelBack = 0x00AFAF, .keyLabelText = 0x000000,
		.keyBarBack = 0xAFD7D7 } },
	// True colour, in the cyan of glacier ice
	{ "glacier", "Glacier", ColourTheme::Polarity::Light, {
		.menuBack = 0x006A8A, .menuText = 0xEAF8FC, .menuShortcut = 0xB8F1FF, .menuSelectedBack = 0x7FD6EA, .menuSelectedText = 0x06303D, .menuSelectedShortcut = 0x5B2BB5,
		.disabledText = 0x7FA3B0,
		.panelBack = 0xE0F1F6, .framePassive = 0x9CC3D0, .frameActive = 0x006A8A, .frameIcons = 0x006B4A,
		.fileText = 0x14323E, .fileDetailText = 0x4A6875, .selectedText = 0x6D3FC4, .cursorBack = 0x006A8A, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xF2E2FF, .columnHeader = 0x0087AF,
		.statusText = 0x5A7A87, .separator = 0xC4DEE7, .cursorSeparator = 0x4FA3BD, .tabBack = 0x7FD6EA, .tabText = 0x06303D,
		.dialogBack = 0xCFE6ED, .dialogText = 0x12303A, .dialogFrame = 0x006A8A, .dialogShortcut = 0x6D3FC4, .focusText = 0x004A63,
		.buttonBack = 0x8FC6D8, .buttonText = 0x06303D, .buttonDefaultText = 0x4A1F9E, .buttonShortcut = 0x5A2DB0, .buttonShadow = 0x86A9B6,
		.controlBack = 0xE0F1F6, .controlText = 0x12303A, .inputBack = 0xF6FCFE, .inputText = 0x06303D, .scrollBarText = 0x9CC3D0, .scrollBarBack = 0xD3E9EF,
		.selectionText = 0x06303D,
		.bottomBack = 0x006A8A, .hintText = 0xA9D3DF, .hintKey = 0xFFFFFF, .keyBack = 0x7FD6EA, .keyText = 0x06303D, .keyLabelBack = 0x006A8A, .keyLabelText = 0xEAF8FC,
		.keyBarBack = 0x006A8A } },
	// True colour, mostly from the Solarized Light palette (MIT licence)
	{ "solar", "Solar", ColourTheme::Polarity::Light, {
		.menuBack = 0xEEE8D5, .menuText = 0x586E75, .menuShortcut = 0xCB4B16, .menuSelectedBack = 0x268BD2, .menuSelectedText = 0xFDF6E3, .menuSelectedShortcut = 0x002B36,
		.disabledText = 0x93A1A1,
		.panelBack = 0xFDF6E3, .framePassive = 0xC9C2AE, .frameActive = 0x586E75, .frameIcons = 0x859900,
		.fileText = 0x586E75, .fileDetailText = 0x586E75, .selectedText = 0xCB4B16, .cursorBack = 0xE4DCC4, .cursorText = 0x073642, .cursorSelectedText = 0xB53F10, .columnHeader = 0x268BD2,
		.statusText = 0x93A1A1, .separator = 0xEEE8D5, .cursorSeparator = 0xCFC6AC, .tabBack = 0xE4DCC4, .tabText = 0x073642,
		.dialogBack = 0xEEE8D5, .dialogText = 0x586E75, .dialogFrame = 0x073642, .dialogShortcut = 0xCB4B16, .focusText = 0x268BD2,
		.buttonBack = 0xDDD6C1, .buttonText = 0x073642, .buttonDefaultText = 0x268BD2, .buttonShortcut = 0xCB4B16, .buttonShadow = 0xC9C2AE,
		.controlBack = 0xFDF6E3, .controlText = 0x586E75, .inputBack = 0xFFFBF0, .inputText = 0x073642, .scrollBarText = 0xC9C2AE, .scrollBarBack = 0xF5EFDC,
		.selectionText = 0xFDF6E3,
		.bottomBack = 0xF5EFDC, .hintText = 0x93A1A1, .hintKey = 0x2AA198, .keyBack = 0x2AA198, .keyText = 0xFDF6E3, .keyLabelBack = 0xE6DFC9, .keyLabelText = 0x586E75,
		.keyBarBack = 0xF5EFDC } },
	// True colour
	{ "mint", "Mint", ColourTheme::Polarity::Light, {
		.menuBack = 0xDCEBE0, .menuText = 0x24332B, .menuShortcut = 0xB03A69, .menuSelectedBack = 0x2E7D5B, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xF8C8DA,
		.disabledText = 0x8FA89A,
		.panelBack = 0xF1F8F2, .framePassive = 0xAFC8B7, .frameActive = 0x2F4A3C, .frameIcons = 0x2E7D5B,
		.fileText = 0x2F3F36, .fileDetailText = 0x44544B, .selectedText = 0xB03A69, .cursorBack = 0xC9E8D3, .cursorText = 0x12201A, .cursorSelectedText = 0x9A2C58, .columnHeader = 0x2E7D5B,
		.statusText = 0x6B8577, .separator = 0xDDEBE1, .cursorSeparator = 0xA6D3B5, .tabBack = 0xC9E8D3, .tabText = 0x12201A,
		.dialogBack = 0xDCEBE0, .dialogText = 0x24332B, .dialogFrame = 0x24332B, .dialogShortcut = 0xB03A69, .focusText = 0x1F5E43,
		.buttonBack = 0xBBD8C4, .buttonText = 0x12201A, .buttonDefaultText = 0x1F5E43, .buttonShortcut = 0xB03A69, .buttonShadow = 0x9DB8A6,
		.controlBack = 0xF1F8F2, .controlText = 0x24332B, .inputBack = 0xFFFFFF, .inputText = 0x12201A, .scrollBarText = 0xAFC8B7, .scrollBarBack = 0xE6F2E9,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xE6F2E9, .hintText = 0x6B8577, .hintKey = 0x2E7D5B, .keyBack = 0x2E7D5B, .keyText = 0xFFFFFF, .keyLabelBack = 0xD3E6D9, .keyLabelText = 0x24332B,
		.keyBarBack = 0xE6F2E9 } },
	// True colour, banded like green bar printer paper
	{ "greenbar", "Greenbar", ColourTheme::Polarity::Light, {
		.menuBack = 0xE3EFE0, .menuText = 0x23302A, .menuShortcut = 0xB0461A, .menuSelectedBack = 0x2E6B4A, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD9A0,
		.disabledText = 0x97A89C,
		.panelBack = 0xFAFAF2, .rowBandBack = 0xDCEBD9, .rowsPerBand = 3, .framePassive = 0xB5CDB3, .frameActive = 0x2E6B4A, .frameIcons = 0x2E6B4A,
		.fileText = 0x23302A, .fileDetailText = 0x5A6B60, .selectedText = 0xB0461A, .cursorBack = 0x2E6B4A, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFD9A0, .columnHeader = 0x2E6B4A,
		.statusText = 0x6F8076, .separator = 0xC3D8C1, .cursorSeparator = 0x5F9479, .tabBack = 0xCDE3CA, .tabText = 0x14251B,
		.dialogBack = 0xEDF3EA, .dialogText = 0x23302A, .dialogFrame = 0x23302A, .dialogShortcut = 0xB0461A, .focusText = 0x1F5A3C,
		.buttonBack = 0xC5DCC2, .buttonText = 0x14251B, .buttonDefaultText = 0x8A3310, .buttonShortcut = 0xB0461A, .buttonShadow = 0xA9BFA7,
		.controlBack = 0xFAFAF2, .controlText = 0x23302A, .inputBack = 0xFFFFFF, .inputText = 0x14251B, .scrollBarText = 0xB5CDB3, .scrollBarBack = 0xEDF3EA,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xEDF3EA, .hintText = 0x6F8076, .hintKey = 0x2E6B4A, .keyBack = 0x2E6B4A, .keyText = 0xFFFFFF, .keyLabelBack = 0xDCEBD9, .keyLabelText = 0x23302A,
		.keyBarBack = 0xEDF3EA } },
	// True colour, banded like ruled paper: the column separators are its red margin line
	{ "notebook", "Notebook", ColourTheme::Polarity::Light, {
		.menuBack = 0xEEF3FA, .menuText = 0x1B2A5C, .menuShortcut = 0xC62828, .menuSelectedBack = 0x1B2A5C, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFB3B3,
		.disabledText = 0x9AA5BD,
		.panelBack = 0xFFFFFF, .rowBandBack = 0xE8F1FB, .rowsPerBand = 1, .framePassive = 0xB9C6DD, .frameActive = 0x1B2A5C, .frameIcons = 0xC62828,
		.fileText = 0x1B2A5C, .fileDetailText = 0x5A6788, .selectedText = 0xC62828, .cursorBack = 0x1B2A5C, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFB3B3, .columnHeader = 0x1B2A5C,
		.statusText = 0x7A86A3, .separator = 0xF2A7A7, .cursorSeparator = 0x6E7CB0, .tabBack = 0xD5E3F5, .tabText = 0x1B2A5C,
		.dialogBack = 0xEEF3FA, .dialogText = 0x1B2A5C, .dialogFrame = 0x1B2A5C, .dialogShortcut = 0xC62828, .focusText = 0x0B3FB5,
		.buttonBack = 0xC9D8EE, .buttonText = 0x14204A, .buttonDefaultText = 0x8E1414, .buttonShortcut = 0xB01E1E, .buttonShadow = 0xA9B6CE,
		.controlBack = 0xFFFFFF, .controlText = 0x1B2A5C, .inputBack = 0xFFFFFF, .inputText = 0x14204A, .scrollBarText = 0xB9C6DD, .scrollBarBack = 0xEEF3FA,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xFFFFFF, .hintText = 0x7A86A3, .hintKey = 0xC62828, .keyBack = 0x1B2A5C, .keyText = 0xFFFFFF, .keyLabelBack = 0xE8F1FB, .keyLabelText = 0x1B2A5C,
		.keyBarBack = 0xFFFFFF } },
	// True colour, banded like the pages of an account book
	{ "daybook", "Daybook", ColourTheme::Polarity::Light, {
		.menuBack = 0xEFE2C2, .menuText = 0x33261A, .menuShortcut = 0xA8231A, .menuSelectedBack = 0x4A3524, .menuSelectedText = 0xFFF6E0, .menuSelectedShortcut = 0xFFC46B,
		.disabledText = 0xA3927A,
		.panelBack = 0xFBF3DF, .rowBandBack = 0xF0E3C4, .rowsPerBand = 2, .framePassive = 0xC9B58C, .frameActive = 0x4A3524, .frameIcons = 0x2F6B3A,
		.fileText = 0x33261A, .fileDetailText = 0x6B5843, .selectedText = 0xA8231A, .cursorBack = 0x4A3524, .cursorText = 0xFFF6E0, .cursorSelectedText = 0xFFB0A0, .columnHeader = 0x7A4A12,
		.statusText = 0x8A765C, .separator = 0xDCCBA6, .cursorSeparator = 0x8A6F55, .tabBack = 0xE3D2AC, .tabText = 0x2A1C0E,
		.dialogBack = 0xF4EACD, .dialogText = 0x33261A, .dialogFrame = 0x4A3524, .dialogShortcut = 0xA8231A, .focusText = 0x1D4E89,
		.buttonBack = 0xDCCBA6, .buttonText = 0x2A1C0E, .buttonDefaultText = 0x8E1B14, .buttonShortcut = 0xA8231A, .buttonShadow = 0xBBA983,
		.controlBack = 0xFBF3DF, .controlText = 0x33261A, .inputBack = 0xFFFDF6, .inputText = 0x1E1408, .scrollBarText = 0xC9B58C, .scrollBarBack = 0xEFE2C2,
		.selectionText = 0xFFF6E0,
		.bottomBack = 0xEFE2C2, .hintText = 0x7A6750, .hintKey = 0xA8231A, .keyBack = 0x4A3524, .keyText = 0xFFF6E0, .keyLabelBack = 0xF0E3C4, .keyLabelText = 0x33261A,
		.keyBarBack = 0xEFE2C2 } },
	// True colour
	{ "blossom", "Blossom", ColourTheme::Polarity::Light, {
		.menuBack = 0xF1DFE6, .menuText = 0x3A2E34, .menuShortcut = 0x8E3B6B, .menuSelectedBack = 0x8E3B6B, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD6E7,
		.disabledText = 0xA8939D,
		.panelBack = 0xFBF4F6, .framePassive = 0xD9BFCA, .frameActive = 0x4A3A42, .frameIcons = 0x2F6BCB,
		.fileText = 0x3A2E34, .fileDetailText = 0x5B4F55, .selectedText = 0x2F6BCB, .cursorBack = 0xF3D3DE, .cursorText = 0x24181E, .cursorSelectedText = 0x2458AE, .columnHeader = 0x8E3B6B,
		.statusText = 0x8C7480, .separator = 0xF0E1E7, .cursorSeparator = 0xE2B5C6, .tabBack = 0xF3D3DE, .tabText = 0x24181E,
		.dialogBack = 0xF1DFE6, .dialogText = 0x3A2E34, .dialogFrame = 0x3A2E34, .dialogShortcut = 0x8E3B6B, .focusText = 0x6A2550,
		.buttonBack = 0xE3C3D0, .buttonText = 0x24181E, .buttonDefaultText = 0x6A2550, .buttonShortcut = 0x8E3B6B, .buttonShadow = 0xCDAFBC,
		.controlBack = 0xFBF4F6, .controlText = 0x3A2E34, .inputBack = 0xFFFFFF, .inputText = 0x24181E, .scrollBarText = 0xD9BFCA, .scrollBarBack = 0xF6EAEF,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xF6EAEF, .hintText = 0x8C7480, .hintKey = 0x8E3B6B, .keyBack = 0x8E3B6B, .keyText = 0xFFFFFF, .keyLabelBack = 0xEDD5DF, .keyLabelText = 0x3A2E34,
		.keyBarBack = 0xF6EAEF } },
};
static_assert(std::ranges::all_of(themes, [](const ColourTheme& theme) { return theme.parameters.rowsPerBand > 0; }));

} // namespace

std::span<const ColourTheme> colourThemes()
{
	return themes;
}

const ColourTheme& defaultColourTheme()
{
	return themes[0];
}

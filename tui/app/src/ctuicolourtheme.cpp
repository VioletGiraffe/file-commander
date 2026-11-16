#include "ctuicolourtheme.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace {

// Colours are in the xterm 256-colour cube or grey ramp: a 256-colour terminal shows them exactly, and no terminal colour scheme redefines them.
// The exceptions are marked "off-palette", or the whole theme "true colour": a 256-colour terminal shows the nearest colour in the palette.
constexpr TColorRGB black{ 0x000000 }, navy{ 0x00005F }, mediumBlue{ 0x0000AF }, red{ 0xAF0000 }, amber{ 0xD78700 }, cyan{ 0x00AFAF }, teal{ 0x008787 },
	darkTeal{ 0x005F5F }, darkGrey{ 0x5F5F5F }, grey{ 0xAFAFAF }, lightGreen{ 0x5FFF5F }, lightCyan{ 0x5FFFFF }, yellow{ 0xFFFF5F }, white{ 0xFFFFFF };

constexpr ColourTheme themes[]{
	{ "classic-blue", "~C~lassic blue", ColourTheme::Polarity::Dark, {
		.menuBack = grey, .menuText = black, .menuShortcut = red, .menuSelectedBack = amber, .menuSelectedText = black, .menuSelectedShortcut = navy,
		.disabledText = darkGrey,
		.panelBack = navy, .framePassive = grey, .frameActive = white, .frameIcons = lightGreen,
		.fileText = lightCyan, .selectedText = yellow, .cursorBack = teal, .cursorText = black, .cursorSelectedText = yellow, .columnHeader = white,
		.statusText = grey, .separator = mediumBlue, .cursorSeparator = darkTeal, .tabBack = cyan, .tabText = black,
		.dialogBack = grey, .dialogText = black, .dialogFrame = white, .dialogShortcut = yellow, .focusText = white,
		.buttonBack = amber, .buttonText = black, .buttonDefaultText = navy, .buttonShortcut = yellow, .buttonShadow = black,
		.controlBack = cyan, .controlText = black, .inputBack = navy, .inputText = white, .scrollBarText = cyan, .scrollBarBack = navy, .selectionText = white,
		.bottomBack = black, .hintText = grey, .hintKey = cyan, .keyBack = yellow, .keyText = black, .keyLabelBack = grey, .keyLabelText = black,
		.keyBarBack = black } },
	{ "dark", "~D~ark", ColourTheme::Polarity::Dark, {
		.menuBack = 0x303030, .menuText = 0xD0D0D0, .menuShortcut = 0xFFAF00, .menuSelectedBack = 0x005F87, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD75F,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x121212, .framePassive = 0x585858, .frameActive = 0xD0D0D0, .frameIcons = 0x5FD75F,
		.fileText = 0xBCBCBC, .selectedText = 0xFFD75F, .cursorBack = 0x005F87, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFD75F, .columnHeader = 0x5FAFD7,
		.statusText = 0x8A8A8A, .separator = 0x303030, .cursorSeparator = 0x5F87AF, .tabBack = 0x4E4E4E, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFFAF00, .focusText = 0xFFFFFF,
		.buttonBack = 0x4E4E4E, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD75F, .buttonShortcut = 0xFFAF00, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0, .inputBack = 0x121212, .inputText = 0xFFFFFF, .scrollBarText = 0x585858, .scrollBarBack = 0x1C1C1C,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x000000, .hintText = 0x8A8A8A, .hintKey = 0xFFAF00, .keyBack = 0xFFD75F, .keyText = 0x000000, .keyLabelBack = 0x005F87, .keyLabelText = 0xFFFFFF,
		.keyBarBack = 0x000000 } },
	{ "amber", "~A~mber", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3A3A3A, .menuText = 0xFFAF00, .menuShortcut = 0xFFFFFF, .menuSelectedBack = 0xD78700, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0xFFFFFF,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x121212, .framePassive = 0x875F00, .frameActive = 0xFFAF00, .frameIcons = 0xFFD787,
		.fileText = 0xD78700, .selectedText = 0xFFFFFF, .cursorBack = 0x875F00, .cursorText = 0xFFD787, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xFFD787,
		.statusText = 0xAF8700, .separator = 0x3A3A3A, .cursorSeparator = 0xAF8700, .tabBack = 0x875F00, .tabText = 0xFFD787,
		.dialogBack = 0x3A3A3A, .dialogText = 0xFFAF00, .dialogFrame = 0xFFD787, .dialogShortcut = 0xFFFFFF, .focusText = 0xFFFFFF,
		.buttonBack = 0x875F00, .buttonText = 0xFFD787, .buttonDefaultText = 0xFFFFFF, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x000000,
		.controlBack = 0x262626, .controlText = 0xFFAF00, .inputBack = 0x121212, .inputText = 0xFFD787, .scrollBarText = 0x875F00, .scrollBarBack = 0x1C1C1C,
		.selectionText = 0x000000,
		.bottomBack = 0x000000, .hintText = 0xAF8700, .hintKey = 0xFFD787, .keyBack = 0xFFAF00, .keyText = 0x000000, .keyLabelBack = 0x3A3A3A, .keyLabelText = 0xFFAF00,
		.keyBarBack = 0x000000 } },
	{ "purple-night", "~P~urple night", ColourTheme::Polarity::Dark, {
		.menuBack = 0x303030, .menuText = 0xD0D0D0, .menuShortcut = 0xFF87D7, .menuSelectedBack = 0x875FD7, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD7FF,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x1C1C1C, .framePassive = 0x5F5F87, .frameActive = 0xD0D0D0, .frameIcons = 0xAF87FF,
		.fileText = 0xD0D0D0, .selectedText = 0xFF87D7, .cursorBack = 0x5F5F87, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFAFD7, .columnHeader = 0xAF87FF,
		.statusText = 0x8A8A8A, .separator = 0x303030, .cursorSeparator = 0x8787AF, .tabBack = 0x5F5F87, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFF87D7, .focusText = 0xFFFFFF,
		.buttonBack = 0x5F5F87, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD7FF, .buttonShortcut = 0xFF87D7, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0, .inputBack = 0x1C1C1C, .inputText = 0xFFFFFF, .scrollBarText = 0x5F5F87, .scrollBarBack = 0x262626,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x000000, .hintText = 0x8A8A8A, .hintKey = 0xAF87FF, .keyBack = 0xFF87D7, .keyText = 0x000000, .keyLabelBack = 0x5F5F87, .keyLabelText = 0xFFFFFF,
		.keyBarBack = 0x000000 } },
	{ "deep-teal", "Deep ~t~eal", ColourTheme::Polarity::Dark, {
		.menuBack = 0x008787, .menuText = 0xFFFFFF, .menuShortcut = 0xFFD75F, .menuSelectedBack = 0x00AFAF, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0x00005F,
		.disabledText = 0x87AFAF,
		.panelBack = 0x002828, // Off-palette
		.framePassive = 0x5F8787, .frameActive = 0xFFFFFF, .frameIcons = 0x5FD75F,
		.fileText = 0xAFD7D7, .selectedText = 0xFFD75F, .cursorBack = 0x00AFAF, .cursorText = 0x000000, .cursorSelectedText = 0xFFFF5F, .columnHeader = 0xFFFFFF,
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
	// True colour, built on the films' code-rain greens
	{ "matrix", "~M~atrix", ColourTheme::Polarity::Dark, {
		.menuBack = 0x030A05, .menuText = 0x20C94A, .menuShortcut = 0xB6FFC8, .menuSelectedBack = 0x00E03A, .menuSelectedText = 0x021006, .menuSelectedShortcut = 0x004D14,
		.disabledText = 0x1A6B2E,
		.panelBack = 0x050C07, .framePassive = 0x0B4A1A, .frameActive = 0x00FF41, .frameIcons = 0xB6FFC8,
		.fileText = 0x14B83C, .selectedText = 0xE4FFEA, .cursorBack = 0x00E03A, .cursorText = 0x021006, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0x9DFFB4,
		.statusText = 0x0F8F2E, .separator = 0x0C2412, .cursorSeparator = 0x00A12A, .tabBack = 0x0A3D16, .tabText = 0x58FF80,
		.dialogBack = 0x071209, .dialogText = 0x20C94A, .dialogFrame = 0x00FF41, .dialogShortcut = 0xB6FFC8, .focusText = 0xE4FFEA,
		.buttonBack = 0x0A3D16, .buttonText = 0x58FF80, .buttonDefaultText = 0xD6FFE0, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x010402,
		.controlBack = 0x0A1A0E, .controlText = 0x20C94A, .inputBack = 0x020703, .inputText = 0x58FF80, .scrollBarText = 0x0B4A1A, .scrollBarBack = 0x030A05,
		.selectionText = 0x021006,
		.bottomBack = 0x000000, .hintText = 0x0F8F2E, .hintKey = 0x58FF80, .keyBack = 0x00FF41, .keyText = 0x021006, .keyLabelBack = 0x0A2E13, .keyLabelText = 0x3BE865,
		.keyBarBack = 0x000000 } },
	// True colour, in the yellow-green of P1 monitor phosphor
	{ "phosphor", "P~h~osphor", ColourTheme::Polarity::Dark, {
		.menuBack = 0x0F2600, .menuText = 0x7BFF3A, .menuShortcut = 0xF2FFE0, .menuSelectedBack = 0x5BFF1F, .menuSelectedText = 0x0A1400, .menuSelectedShortcut = 0x1E5200,
		.disabledText = 0x2E5A10,
		.panelBack = 0x000000, .framePassive = 0x1E5200, .frameActive = 0x5BFF1F, .frameIcons = 0xC8FF9A,
		.fileText = 0x34B000, .selectedText = 0xF2FFE0, .cursorBack = 0x1E5200, .cursorText = 0x7BFF3A, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xC8FF9A,
		.statusText = 0x2E8F00, .separator = 0x142E00, .cursorSeparator = 0x2E7A00, .tabBack = 0x1E5200, .tabText = 0x9BFF6A,
		.dialogBack = 0x0A1A00, .dialogText = 0x7BFF3A, .dialogFrame = 0x5BFF1F, .dialogShortcut = 0xF2FFE0, .focusText = 0xF2FFE0,
		.buttonBack = 0x1E5200, .buttonText = 0x9BFF6A, .buttonDefaultText = 0xF2FFE0, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x000000,
		.controlBack = 0x0F2600, .controlText = 0x7BFF3A, .inputBack = 0x000000, .inputText = 0x9BFF6A, .scrollBarText = 0x1E5200, .scrollBarBack = 0x0A1A00,
		.selectionText = 0x0A1400,
		.bottomBack = 0x0A1A00, .hintText = 0x3CD000, .hintKey = 0xC8FF9A, .keyBack = 0x5BFF1F, .keyText = 0x0A1400, .keyLabelBack = 0x1E5200, .keyLabelText = 0xC8FF9A,
		.keyBarBack = 0x000000 } },
	// True colour, in an aqua phosphor green
	{ "vector", "~V~ector", ColourTheme::Polarity::Dark, {
		.menuBack = 0x000000, .menuText = 0x19D99A, .menuShortcut = 0xB5FFE6, .menuSelectedBack = 0x00FFB0, .menuSelectedText = 0x00140E, .menuSelectedShortcut = 0x0B4A38,
		.disabledText = 0x0E5A44,
		.panelBack = 0x000000, .framePassive = 0x0E5A44, .frameActive = 0x00FFB0, .frameIcons = 0xB5FFE6,
		.fileText = 0x14B884, .selectedText = 0xECFFF8, .cursorBack = 0x063A2C, .cursorText = 0x4DFFC4, .cursorSelectedText = 0xFFFFFF, .columnHeader = 0xB5FFE6,
		.statusText = 0x13906A, .separator = 0x061A15, .cursorSeparator = 0x0E5A44, .tabBack = 0x063A2C, .tabText = 0x4DFFC4,
		.dialogBack = 0x000000, .dialogText = 0x19D99A, .dialogFrame = 0x00FFB0, .dialogShortcut = 0xB5FFE6, .focusText = 0xECFFF8,
		.buttonBack = 0x04241C, .buttonText = 0x4DFFC4, .buttonDefaultText = 0xECFFF8, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x04140F,
		.controlBack = 0x04140F, .controlText = 0x19D99A, .inputBack = 0x04140F, .inputText = 0x4DFFC4, .scrollBarText = 0x0E5A44, .scrollBarBack = 0x000000,
		.selectionText = 0x00140E,
		.bottomBack = 0x000000, .hintText = 0x13906A, .hintKey = 0x4DFFC4, .keyBack = 0x063A2C, .keyText = 0x4DFFC4, .keyLabelBack = 0x000000, .keyLabelText = 0x19D99A,
		.keyBarBack = 0x000000 } },
	// True colour, mostly from the Nord palette (MIT licence)
	{ "slate", "~S~late", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3B4252, .menuText = 0xD8DEE9, .menuShortcut = 0x88C0D0, .menuSelectedBack = 0x5E81AC, .menuSelectedText = 0xECEFF4, .menuSelectedShortcut = 0xEBCB8B,
		.disabledText = 0x616E88,
		.panelBack = 0x2E3440, .framePassive = 0x4C566A, .frameActive = 0xD8DEE9, .frameIcons = 0xA3BE8C,
		.fileText = 0xD8DEE9, .selectedText = 0xEBCB8B, .cursorBack = 0x5E81AC, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFE4A8, .columnHeader = 0x81A1C1,
		.statusText = 0x7B88A1, .separator = 0x3B4252, .cursorSeparator = 0x81A1C1, .tabBack = 0x4C566A, .tabText = 0xECEFF4,
		.dialogBack = 0x3B4252, .dialogText = 0xD8DEE9, .dialogFrame = 0xECEFF4, .dialogShortcut = 0x88C0D0, .focusText = 0x88C0D0,
		.buttonBack = 0x5A6480, .buttonText = 0xECEFF4, .buttonDefaultText = 0xEBCB8B, .buttonShortcut = 0x88C0D0, .buttonShadow = 0x242933,
		.controlBack = 0x434C5E, .controlText = 0xD8DEE9, .inputBack = 0x2E3440, .inputText = 0xECEFF4, .scrollBarText = 0x4C566A, .scrollBarBack = 0x2E3440,
		.selectionText = 0xECEFF4,
		.bottomBack = 0x242933, .hintText = 0x7B88A1, .hintKey = 0x88C0D0, .keyBack = 0x88C0D0, .keyText = 0x2E3440, .keyLabelBack = 0x3B4252, .keyLabelText = 0xD8DEE9,
		.keyBarBack = 0x242933 } },
	// True colour, mostly from the Gruvbox palette (MIT licence)
	{ "ember", "~E~mber", ColourTheme::Polarity::Dark, {
		.menuBack = 0x3C3836, .menuText = 0xEBDBB2, .menuShortcut = 0xFE8019, .menuSelectedBack = 0xD65D0E, .menuSelectedText = 0xFBF1C7, .menuSelectedShortcut = 0xFABD2F,
		.disabledText = 0x7C6F64,
		.panelBack = 0x1D2021, .framePassive = 0x504945, .frameActive = 0xEBDBB2, .frameIcons = 0xB8BB26,
		.fileText = 0xD5C4A1, .selectedText = 0xFABD2F, .cursorBack = 0x504945, .cursorText = 0xFBF1C7, .cursorSelectedText = 0xFABD2F, .columnHeader = 0x83A598,
		.statusText = 0xA89984, .separator = 0x32302F, .cursorSeparator = 0x665C54, .tabBack = 0x504945, .tabText = 0xFBF1C7,
		.dialogBack = 0x3C3836, .dialogText = 0xEBDBB2, .dialogFrame = 0xFBF1C7, .dialogShortcut = 0xFE8019, .focusText = 0xFABD2F,
		.buttonBack = 0x504945, .buttonText = 0xFBF1C7, .buttonDefaultText = 0xFABD2F, .buttonShortcut = 0xFE8019, .buttonShadow = 0x141617,
		.controlBack = 0x32302F, .controlText = 0xEBDBB2, .inputBack = 0x1D2021, .inputText = 0xFBF1C7, .scrollBarText = 0x665C54, .scrollBarBack = 0x282828,
		.selectionText = 0xFBF1C7,
		.bottomBack = 0x141617, .hintText = 0xA89984, .hintKey = 0xFE8019, .keyBack = 0xFABD2F, .keyText = 0x1D2021, .keyLabelBack = 0x3C3836, .keyLabelText = 0xEBDBB2,
		.keyBarBack = 0x141617 } },
	// True colour, mostly from the Tokyo Night palette (MIT licence)
	{ "midnight", "M~i~dnight", ColourTheme::Polarity::Dark, {
		.menuBack = 0x16161E, .menuText = 0xA9B1D6, .menuShortcut = 0xBB9AF7, .menuSelectedBack = 0x3D59A1, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xE0AF68,
		.disabledText = 0x545C7E,
		.panelBack = 0x1A1B26, .framePassive = 0x3B4261, .frameActive = 0xC0CAF5, .frameIcons = 0x9ECE6A,
		.fileText = 0xC0CAF5, .selectedText = 0xE0AF68, .cursorBack = 0x2F3B66, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFC777, .columnHeader = 0x7AA2F7,
		.statusText = 0x565F89, .separator = 0x24283B, .cursorSeparator = 0x3D59A1, .tabBack = 0x3B4261, .tabText = 0xFFFFFF,
		.dialogBack = 0x24283B, .dialogText = 0xC0CAF5, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xBB9AF7, .focusText = 0x7DCFFF,
		.buttonBack = 0x3B4261, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xE0AF68, .buttonShortcut = 0xBB9AF7, .buttonShadow = 0x101018,
		.controlBack = 0x292E42, .controlText = 0xC0CAF5, .inputBack = 0x1A1B26, .inputText = 0xFFFFFF, .scrollBarText = 0x3B4261, .scrollBarBack = 0x1A1B26,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x101018, .hintText = 0x565F89, .hintKey = 0xBB9AF7, .keyBack = 0x7AA2F7, .keyText = 0x16161E, .keyLabelBack = 0x24283B, .keyLabelText = 0xC0CAF5,
		.keyBarBack = 0x101018 } },
	{ "light", "~L~ight", ColourTheme::Polarity::Light, {
		.menuBack = 0xD0D0D0, .menuText = 0x262626, .menuShortcut = 0xAF5F00, .menuSelectedBack = 0x0087AF, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD75F,
		.disabledText = 0x8A8A8A,
		.panelBack = 0xEEEEEE, .framePassive = 0xA8A8A8, .frameActive = 0x444444, .frameIcons = 0x008700,
		.fileText = 0x444444, .selectedText = 0xD75F00, .cursorBack = 0xAFD7FF, .cursorText = 0x000000, .cursorSelectedText = 0xAF5F00, .columnHeader = 0x0087AF,
		.statusText = 0x6C6C6C, .separator = 0xDADADA, .cursorSeparator = 0x87AFD7, .tabBack = 0xAFD7FF, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x262626, .dialogFrame = 0x262626, .dialogShortcut = 0xAF0000, .focusText = 0x005F87,
		.buttonBack = 0xA8A8A8, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xAF0000, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xEEEEEE, .controlText = 0x262626, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0xA8A8A8, .scrollBarBack = 0xE4E4E4,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xEEEEEE, .hintText = 0x6C6C6C, .hintKey = 0x0087AF, .keyBack = 0x0087AF, .keyText = 0xFFFFFF, .keyLabelBack = 0xD0D0D0, .keyLabelText = 0x262626,
		.keyBarBack = 0xEEEEEE } },
	{ "cool-grey", "C~o~ol grey", ColourTheme::Polarity::Light, {
		.menuBack = 0xBCBCBC, .menuText = 0x000000, .menuShortcut = 0x0000AF, .menuSelectedBack = 0xFFD787, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0x0000AF,
		.disabledText = 0x808080,
		.panelBack = 0xE4E4E4, .framePassive = 0x9E9E9E, .frameActive = 0x262626, .frameIcons = 0x008700,
		.fileText = 0x262626, .selectedText = 0x0000AF, .cursorBack = 0xFFD787, .cursorText = 0x000000, .cursorSelectedText = 0x0000AF, .columnHeader = 0x005F87,
		.statusText = 0x585858, .separator = 0xC6C6C6, .cursorSeparator = 0xD7AF5F, .tabBack = 0x87AFD7, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x000000, .dialogFrame = 0x000000, .dialogShortcut = 0xAF0000, .focusText = 0x0000AF,
		.buttonBack = 0x87AFD7, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xAF0000, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xEEEEEE, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0x9E9E9E, .scrollBarBack = 0xE4E4E4,
		.selectionText = 0x000000,
		.bottomBack = 0xE4E4E4, .hintText = 0x585858, .hintKey = 0x0000AF, .keyBack = 0xFFD787, .keyText = 0x000000, .keyLabelBack = 0xBCBCBC, .keyLabelText = 0x000000,
		.keyBarBack = 0xE4E4E4 } },
	{ "light-cyan", "Light c~y~an", ColourTheme::Polarity::Light, {
		.menuBack = 0xAFD7D7, .menuText = 0x000000, .menuShortcut = 0xAF0000, .menuSelectedBack = 0x00005F, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFFF5F,
		.disabledText = 0x5F8787,
		.panelBack = 0xD7FFFF, .framePassive = 0x5F8787, .frameActive = 0x00005F, .frameIcons = 0x005F00,
		.fileText = 0x00005F, .selectedText = 0xAF0000, .cursorBack = 0x00AFAF, .cursorText = 0x000000, .cursorSelectedText = 0xFFFF5F, .columnHeader = 0x005FAF,
		.statusText = 0x005F5F, .separator = 0xAFD7D7, .cursorSeparator = 0x008787, .tabBack = 0x00AFAF, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x000000, .dialogFrame = 0x00005F, .dialogShortcut = 0xAF0000, .focusText = 0x00005F,
		.buttonBack = 0x00AFAF, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xAFD7D7, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x00005F, .scrollBarText = 0x5F8787, .scrollBarBack = 0xD7FFFF,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xAFD7D7, .hintText = 0x005F5F, .hintKey = 0xAF0000, .keyBack = 0xFFFF5F, .keyText = 0x000000, .keyLabelBack = 0x00AFAF, .keyLabelText = 0x000000,
		.keyBarBack = 0xAFD7D7 } },
	// True colour, mostly from the Solarized Light palette (MIT licence)
	{ "solar", "Sola~r~", ColourTheme::Polarity::Light, {
		.menuBack = 0xEEE8D5, .menuText = 0x586E75, .menuShortcut = 0xCB4B16, .menuSelectedBack = 0x268BD2, .menuSelectedText = 0xFDF6E3, .menuSelectedShortcut = 0x002B36,
		.disabledText = 0x93A1A1,
		.panelBack = 0xFDF6E3, .framePassive = 0xC9C2AE, .frameActive = 0x586E75, .frameIcons = 0x859900,
		.fileText = 0x586E75, .selectedText = 0xCB4B16, .cursorBack = 0xE4DCC4, .cursorText = 0x073642, .cursorSelectedText = 0xB53F10, .columnHeader = 0x268BD2,
		.statusText = 0x93A1A1, .separator = 0xEEE8D5, .cursorSeparator = 0xCFC6AC, .tabBack = 0xE4DCC4, .tabText = 0x073642,
		.dialogBack = 0xEEE8D5, .dialogText = 0x586E75, .dialogFrame = 0x073642, .dialogShortcut = 0xCB4B16, .focusText = 0x268BD2,
		.buttonBack = 0xDDD6C1, .buttonText = 0x073642, .buttonDefaultText = 0x268BD2, .buttonShortcut = 0xCB4B16, .buttonShadow = 0xC9C2AE,
		.controlBack = 0xFDF6E3, .controlText = 0x586E75, .inputBack = 0xFFFBF0, .inputText = 0x073642, .scrollBarText = 0xC9C2AE, .scrollBarBack = 0xF5EFDC,
		.selectionText = 0xFDF6E3,
		.bottomBack = 0xF5EFDC, .hintText = 0x93A1A1, .hintKey = 0x2AA198, .keyBack = 0x2AA198, .keyText = 0xFDF6E3, .keyLabelBack = 0xE6DFC9, .keyLabelText = 0x586E75,
		.keyBarBack = 0xF5EFDC } },
	// True colour
	{ "mint", "Mi~n~t", ColourTheme::Polarity::Light, {
		.menuBack = 0xDCEBE0, .menuText = 0x24332B, .menuShortcut = 0xB03A69, .menuSelectedBack = 0x2E7D5B, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xF8C8DA,
		.disabledText = 0x8FA89A,
		.panelBack = 0xF1F8F2, .framePassive = 0xAFC8B7, .frameActive = 0x2F4A3C, .frameIcons = 0x2E7D5B,
		.fileText = 0x2F3F36, .selectedText = 0xB03A69, .cursorBack = 0xC9E8D3, .cursorText = 0x12201A, .cursorSelectedText = 0x9A2C58, .columnHeader = 0x2E7D5B,
		.statusText = 0x6B8577, .separator = 0xDDEBE1, .cursorSeparator = 0xA6D3B5, .tabBack = 0xC9E8D3, .tabText = 0x12201A,
		.dialogBack = 0xDCEBE0, .dialogText = 0x24332B, .dialogFrame = 0x24332B, .dialogShortcut = 0xB03A69, .focusText = 0x1F5E43,
		.buttonBack = 0xBBD8C4, .buttonText = 0x12201A, .buttonDefaultText = 0x1F5E43, .buttonShortcut = 0xB03A69, .buttonShadow = 0x9DB8A6,
		.controlBack = 0xF1F8F2, .controlText = 0x24332B, .inputBack = 0xFFFFFF, .inputText = 0x12201A, .scrollBarText = 0xAFC8B7, .scrollBarBack = 0xE6F2E9,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xE6F2E9, .hintText = 0x6B8577, .hintKey = 0x2E7D5B, .keyBack = 0x2E7D5B, .keyText = 0xFFFFFF, .keyLabelBack = 0xD3E6D9, .keyLabelText = 0x24332B,
		.keyBarBack = 0xE6F2E9 } },
	// True colour
	{ "blossom", "~B~lossom", ColourTheme::Polarity::Light, {
		.menuBack = 0xF1DFE6, .menuText = 0x3A2E34, .menuShortcut = 0x8E3B6B, .menuSelectedBack = 0x8E3B6B, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD6E7,
		.disabledText = 0xA8939D,
		.panelBack = 0xFBF4F6, .framePassive = 0xD9BFCA, .frameActive = 0x4A3A42, .frameIcons = 0x2F6BCB,
		.fileText = 0x3A2E34, .selectedText = 0x2F6BCB, .cursorBack = 0xF3D3DE, .cursorText = 0x24181E, .cursorSelectedText = 0x2458AE, .columnHeader = 0x8E3B6B,
		.statusText = 0x8C7480, .separator = 0xF0E1E7, .cursorSeparator = 0xE2B5C6, .tabBack = 0xF3D3DE, .tabText = 0x24181E,
		.dialogBack = 0xF1DFE6, .dialogText = 0x3A2E34, .dialogFrame = 0x3A2E34, .dialogShortcut = 0x8E3B6B, .focusText = 0x6A2550,
		.buttonBack = 0xE3C3D0, .buttonText = 0x24181E, .buttonDefaultText = 0x6A2550, .buttonShortcut = 0x8E3B6B, .buttonShadow = 0xCDAFBC,
		.controlBack = 0xFBF4F6, .controlText = 0x3A2E34, .inputBack = 0xFFFFFF, .inputText = 0x24181E, .scrollBarText = 0xD9BFCA, .scrollBarBack = 0xF6EAEF,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xF6EAEF, .hintText = 0x8C7480, .hintKey = 0x8E3B6B, .keyBack = 0x8E3B6B, .keyText = 0xFFFFFF, .keyLabelBack = 0xEDD5DF, .keyLabelText = 0x3A2E34,
		.keyBarBack = 0xF6EAEF } },
};
static_assert(std::ranges::is_sorted(themes, {}, &ColourTheme::polarity));

// Turbo Vision matches a menu shortcut regardless of case
[[nodiscard]] constexpr bool everyThemeHasItsOwnMenuShortcut()
{
	const auto lowerCaseShortcut = [](std::string_view menuName) {
		const size_t tilde = menuName.find('~');
		if (tilde == std::string_view::npos || tilde + 1 == menuName.size())
			return '\0';
		const char shortcut = menuName[tilde + 1];
		return shortcut >= 'A' && shortcut <= 'Z' ? (char)(shortcut - 'A' + 'a') : shortcut;
	};
	for (size_t i = 0; i < std::size(themes); ++i)
	{
		const char shortcut = lowerCaseShortcut(themes[i].menuName);
		if (shortcut == '\0')
			return false;
		for (size_t j = 0; j < i; ++j)
		{
			if (lowerCaseShortcut(themes[j].menuName) == shortcut)
				return false;
		}
	}
	return true;
}
static_assert(everyThemeHasItsOwnMenuShortcut());

} // namespace

std::span<const ColourTheme> colourThemes()
{
	return themes;
}

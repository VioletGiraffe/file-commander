#include "ctuipalette.h"

#define Uses_TApplication
#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <iterator>
#include <span>

namespace {

// A theme's colours by role. The application palette pairs them into its entries.
struct ThemeColours
{
	// Also the hint row's and the key bar's pressed items
	TColorRGB menuBack, menuText, menuShortcut, menuSelectedBack, menuSelectedText, menuSelectedShortcut;
	TColorRGB disabledText; // Everywhere

	TColorRGB panelBack, framePassive, frameActive;
	TColorRGB frameIcons; // Also the arrows of input lines
	TColorRGB fileText, selectedText, cursorBack, cursorText, cursorSelectedText, columnHeader, statusText, separator, cursorSeparator;
	TColorRGB tabBack, tabText; // The active tab and the filter line

	TColorRGB dialogBack, dialogText, dialogFrame, dialogShortcut;
	TColorRGB focusText; // The focused control's label, and the focused button or cluster item
	TColorRGB buttonBack, buttonText, buttonDefaultText, buttonShortcut, buttonShadow;
	TColorRGB controlBack, controlText; // Check boxes, radio buttons and lists
	TColorRGB inputBack, inputText, scrollBarText, scrollBarBack;
	TColorRGB selectionText; // On menuSelectedBack: an input line's selected text, a list's focused item, a pressed key

	TColorRGB bottomBack, hintText, hintKey, keyBack, keyText, keyLabelBack, keyLabelText;
};

struct Theme
{
	const char* id;
	const char* menuName;
	ThemeColours colours;
};

// Every colour is in the xterm 256-colour cube or grey ramp: a 256-colour terminal shows it exactly, and no terminal colour scheme redefines it
constexpr TColorRGB black{ 0x000000 }, navy{ 0x00005F }, mediumBlue{ 0x0000AF }, red{ 0xAF0000 }, amber{ 0xD78700 }, cyan{ 0x00AFAF }, teal{ 0x008787 },
	darkTeal{ 0x005F5F }, darkGrey{ 0x5F5F5F }, grey{ 0xAFAFAF }, lightGreen{ 0x5FFF5F }, lightCyan{ 0x5FFFFF }, yellow{ 0xFFFF5F }, white{ 0xFFFFFF };

// In ColourTheme order
constexpr Theme themes[]{
	{ "classic-blue", "~C~lassic blue", {
		.menuBack = grey, .menuText = black, .menuShortcut = red, .menuSelectedBack = amber, .menuSelectedText = black, .menuSelectedShortcut = navy,
		.disabledText = darkGrey,
		.panelBack = navy, .framePassive = grey, .frameActive = white, .frameIcons = lightGreen,
		.fileText = lightCyan, .selectedText = yellow, .cursorBack = teal, .cursorText = black, .cursorSelectedText = yellow, .columnHeader = white,
		.statusText = grey, .separator = mediumBlue, .cursorSeparator = darkTeal, .tabBack = cyan, .tabText = black,
		.dialogBack = grey, .dialogText = black, .dialogFrame = white, .dialogShortcut = yellow, .focusText = white,
		.buttonBack = amber, .buttonText = black, .buttonDefaultText = navy, .buttonShortcut = yellow, .buttonShadow = black,
		.controlBack = cyan, .controlText = black, .inputBack = navy, .inputText = white, .scrollBarText = cyan, .scrollBarBack = navy, .selectionText = white,
		.bottomBack = black, .hintText = grey, .hintKey = cyan, .keyBack = yellow, .keyText = black, .keyLabelBack = grey, .keyLabelText = black } },
	{ "dark", "~D~ark", {
		.menuBack = 0x303030, .menuText = 0xD0D0D0, .menuShortcut = 0xFFAF00, .menuSelectedBack = 0x005F87, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD75F,
		.disabledText = 0x6C6C6C,
		.panelBack = 0x121212, .framePassive = 0x585858, .frameActive = 0xD0D0D0, .frameIcons = 0x5FD75F,
		.fileText = 0xBCBCBC, .selectedText = 0xFFD75F, .cursorBack = 0x005F87, .cursorText = 0xFFFFFF, .cursorSelectedText = 0xFFD75F, .columnHeader = 0x5FAFD7,
		.statusText = 0x8A8A8A, .separator = 0x303030, .cursorSeparator = 0x5F87AF, .tabBack = 0x4E4E4E, .tabText = 0xFFFFFF,
		.dialogBack = 0x303030, .dialogText = 0xD0D0D0, .dialogFrame = 0xFFFFFF, .dialogShortcut = 0xFFAF00, .focusText = 0xFFFFFF,
		.buttonBack = 0x4E4E4E, .buttonText = 0xFFFFFF, .buttonDefaultText = 0xFFD75F, .buttonShortcut = 0xFFAF00, .buttonShadow = 0x121212,
		.controlBack = 0x3A3A3A, .controlText = 0xD0D0D0, .inputBack = 0x121212, .inputText = 0xFFFFFF, .scrollBarText = 0x585858, .scrollBarBack = 0x1C1C1C,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0x000000, .hintText = 0x8A8A8A, .hintKey = 0xFFAF00, .keyBack = 0xFFD75F, .keyText = 0x000000, .keyLabelBack = 0x005F87, .keyLabelText = 0xFFFFFF } },
	{ "light", "~L~ight", {
		.menuBack = 0xD0D0D0, .menuText = 0x262626, .menuShortcut = 0xAF5F00, .menuSelectedBack = 0x0087AF, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFD75F,
		.disabledText = 0x8A8A8A,
		.panelBack = 0xEEEEEE, .framePassive = 0xA8A8A8, .frameActive = 0x444444, .frameIcons = 0x008700,
		.fileText = 0x444444, .selectedText = 0xD75F00, .cursorBack = 0xAFD7FF, .cursorText = 0x000000, .cursorSelectedText = 0xAF5F00, .columnHeader = 0x0087AF,
		.statusText = 0x6C6C6C, .separator = 0xDADADA, .cursorSeparator = 0x87AFD7, .tabBack = 0xAFD7FF, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x262626, .dialogFrame = 0x262626, .dialogShortcut = 0xAF0000, .focusText = 0x005F87,
		.buttonBack = 0xA8A8A8, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xAF0000, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xEEEEEE, .controlText = 0x262626, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0xA8A8A8, .scrollBarBack = 0xE4E4E4,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xEEEEEE, .hintText = 0x6C6C6C, .hintKey = 0x0087AF, .keyBack = 0x0087AF, .keyText = 0xFFFFFF, .keyLabelBack = 0xD0D0D0, .keyLabelText = 0x262626 } },
	{ "cool-grey", "C~o~ol grey", {
		.menuBack = 0xBCBCBC, .menuText = 0x000000, .menuShortcut = 0x0000AF, .menuSelectedBack = 0xFFD787, .menuSelectedText = 0x000000, .menuSelectedShortcut = 0x0000AF,
		.disabledText = 0x808080,
		.panelBack = 0xE4E4E4, .framePassive = 0x9E9E9E, .frameActive = 0x262626, .frameIcons = 0x008700,
		.fileText = 0x262626, .selectedText = 0x0000AF, .cursorBack = 0xFFD787, .cursorText = 0x000000, .cursorSelectedText = 0x0000AF, .columnHeader = 0x005F87,
		.statusText = 0x585858, .separator = 0xC6C6C6, .cursorSeparator = 0xD7AF5F, .tabBack = 0x87AFD7, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x000000, .dialogFrame = 0x000000, .dialogShortcut = 0xAF0000, .focusText = 0x0000AF,
		.buttonBack = 0x87AFD7, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xAF0000, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xEEEEEE, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x000000, .scrollBarText = 0x9E9E9E, .scrollBarBack = 0xE4E4E4,
		.selectionText = 0x000000,
		.bottomBack = 0xE4E4E4, .hintText = 0x585858, .hintKey = 0x0000AF, .keyBack = 0xFFD787, .keyText = 0x000000, .keyLabelBack = 0xBCBCBC, .keyLabelText = 0x000000 } },
	{ "light-cyan", "Light c~y~an", {
		.menuBack = 0xAFD7D7, .menuText = 0x000000, .menuShortcut = 0xAF0000, .menuSelectedBack = 0x00005F, .menuSelectedText = 0xFFFFFF, .menuSelectedShortcut = 0xFFFF5F,
		.disabledText = 0x5F8787,
		.panelBack = 0xD7FFFF, .framePassive = 0x5F8787, .frameActive = 0x00005F, .frameIcons = 0x005F00,
		.fileText = 0x00005F, .selectedText = 0xAF0000, .cursorBack = 0x00AFAF, .cursorText = 0x000000, .cursorSelectedText = 0xFFFF5F, .columnHeader = 0x005FAF,
		.statusText = 0x005F5F, .separator = 0xAFD7D7, .cursorSeparator = 0x008787, .tabBack = 0x00AFAF, .tabText = 0x000000,
		.dialogBack = 0xD0D0D0, .dialogText = 0x000000, .dialogFrame = 0x00005F, .dialogShortcut = 0xAF0000, .focusText = 0x00005F,
		.buttonBack = 0x00AFAF, .buttonText = 0x000000, .buttonDefaultText = 0x00005F, .buttonShortcut = 0xFFFFFF, .buttonShadow = 0x8A8A8A,
		.controlBack = 0xAFD7D7, .controlText = 0x000000, .inputBack = 0xFFFFFF, .inputText = 0x00005F, .scrollBarText = 0x5F8787, .scrollBarBack = 0xD7FFFF,
		.selectionText = 0xFFFFFF,
		.bottomBack = 0xAFD7D7, .hintText = 0x005F5F, .hintKey = 0xAF0000, .keyBack = 0xFFFF5F, .keyText = 0x000000, .keyLabelBack = 0x00AFAF, .keyLabelText = 0x000000 } },
};
static_assert(std::size(themes) == colourThemeCount && (size_t)ColourTheme::LightCyan + 1 == colourThemeCount);

constexpr char stockApplicationColors[] = cpAppColor;
constexpr size_t panelEntryCount = ppLastEntry - ppFileListRow + 1, hintLineEntryCount = 6, keyBarEntryCount = 7;
// The panel's entries follow Turbo Vision's, then come the hint row's and the key bar's
constexpr uchar firstPanelApplicationEntry = sizeof(stockApplicationColors);
constexpr uchar firstHintLineApplicationEntry = firstPanelApplicationEntry + panelEntryCount;
constexpr uchar firstKeyBarApplicationEntry = firstHintLineApplicationEntry + hintLineEntryCount;
constexpr size_t applicationEntryCount = firstKeyBarApplicationEntry - 1 + keyBarEntryCount;

[[nodiscard]] TPalette applicationPaletteFor(const ThemeColours& c)
{
	// Entry 1: the desktop
	const TColorAttr desktopColors[]{
		{ c.panelBack, c.menuBack },
	};

	// Entries 2-7: the menus and the status lines
	const TColorAttr menuColors[]{
		{ c.menuText, c.menuBack },                         // Normal text
		{ c.disabledText, c.menuBack },                     // Disabled text
		{ c.menuShortcut, c.menuBack },                     // Shortcut
		{ c.menuSelectedText, c.menuSelectedBack },         // Selected
		{ c.disabledText, c.menuSelectedBack },             // Selected disabled
		{ c.menuSelectedShortcut, c.menuSelectedBack },     // Selected shortcut
	};

	// Entries 8-15: blue windows, the panels' type
	const TColorAttr blueWindowColors[]{
		{ c.framePassive, c.panelBack }, // Frame passive
		{ c.frameActive, c.panelBack },  // Frame active
		{ c.frameIcons, c.panelBack },   // Frame icons
		{ c.panelBack, c.tabBack },      // Scroll bar page area
		{ c.panelBack, c.tabBack },      // Scroll bar controls
		{ c.selectedText, c.panelBack }, // Scroller normal text
		{ c.panelBack, c.framePassive }, // Scroller selected text
		{ c.frameActive, c.panelBack },  // Reserved
	};

	// Entries 32-63: gray dialogs, every dialog's type
	const TColorAttr grayDialogColors[]{
		{ c.dialogText, c.dialogBack },             // Frame passive
		{ c.dialogFrame, c.dialogBack },            // Frame active
		{ c.frameIcons, c.dialogBack },             // Frame icons
		{ c.scrollBarText, c.scrollBarBack },       // Scroll bar page area
		{ c.scrollBarText, c.scrollBarBack },       // Scroll bar controls
		{ c.dialogText, c.dialogBack },             // Static text
		{ c.dialogText, c.dialogBack },             // Label normal
		{ c.focusText, c.dialogBack },              // Label selected
		{ c.dialogShortcut, c.dialogBack },         // Label shortcut
		{ c.buttonText, c.buttonBack },             // Button normal
		{ c.buttonDefaultText, c.buttonBack },      // Button default
		{ c.focusText, c.buttonBack },              // Button selected
		{ c.disabledText, c.dialogBack },           // Button disabled
		{ c.buttonShortcut, c.buttonBack },         // Button shortcut
		{ c.buttonShadow, c.dialogBack },           // Button shadow
		{ c.controlText, c.controlBack },           // Cluster normal
		{ c.focusText, c.controlBack },             // Cluster selected
		{ c.dialogShortcut, c.controlBack },        // Cluster shortcut
		{ c.inputText, c.inputBack },               // Input line normal
		{ c.selectionText, c.menuSelectedBack },    // Input line selected
		{ c.frameIcons, c.inputBack },              // Input line arrows
		{ c.buttonText, c.buttonBack },             // History arrow
		{ c.buttonBack, c.dialogBack },             // History sides
		{ c.scrollBarBack, c.scrollBarText },       // History window scroll bar page area
		{ c.scrollBarBack, c.scrollBarText },       // History window scroll bar controls
		{ c.controlText, c.controlBack },           // List viewer normal
		{ c.selectionText, c.menuSelectedBack },    // List viewer focused
		{ c.dialogShortcut, c.controlBack },        // List viewer selected
		{ c.scrollBarBack, c.controlBack },         // List viewer divider
		{ c.scrollBarText, c.scrollBarBack },       // Info pane
		{ c.disabledText, c.controlBack },          // Cluster disabled
		0x00,                                       // Reserved
	};

	// The panel's entries, in PanelPaletteEntry order
	const TColorAttr panelColors[]{
		{ c.fileText, c.panelBack },             // ppFileListRow
		{ c.selectedText, c.panelBack },         // ppFileListSelectedRow
		{ c.cursorText, c.cursorBack },          // ppFileListCursor
		{ c.cursorSelectedText, c.cursorBack },  // ppFileListSelectedCursor
		{ c.columnHeader, c.panelBack },         // ppColumnHeader
		{ c.statusText, c.panelBack },           // ppStatusText
		{ c.tabText, c.tabBack },                // ppActiveTab
		{ c.tabText, c.tabBack },                // ppFilterText
		{ c.frameActive, c.panelBack },          // ppFilterSelectedText
		{ c.frameIcons, c.tabBack },             // ppFilterArrows
		{ c.separator, c.panelBack },            // ppColumnSeparator
		{ c.cursorSeparator, c.cursorBack },     // ppCursorColumnSeparator
	};

	// The hint row's entries
	const TColorAttr hintLineColors[]{
		{ c.hintText, c.bottomBack },                    // Normal text
		{ c.disabledText, c.bottomBack },                // Disabled text
		{ c.hintKey, c.bottomBack },                     // Shortcut
		{ c.menuSelectedText, c.menuSelectedBack },      // Selected
		{ c.disabledText, c.menuSelectedBack },          // Selected disabled
		{ c.menuSelectedShortcut, c.menuSelectedBack },  // Selected shortcut
	};

	// The key bar's entries
	const TColorAttr keyBarColors[]{
		{ c.keyLabelText, c.keyLabelBack },          // Label
		{ c.disabledText, c.keyLabelBack },          // Disabled item
		{ c.keyText, c.keyBack },                    // Key
		{ c.menuSelectedText, c.menuSelectedBack },  // Pressed label
		{ c.disabledText, c.menuSelectedBack },      // Pressed disabled item
		{ c.selectionText, c.menuSelectedBack },     // Pressed key
		{ c.bottomBack, c.bottomBack },              // Background
	};
	static_assert(std::size(menuColors) == 6 && std::size(blueWindowColors) == 8 && std::size(grayDialogColors) == 32);
	static_assert(std::size(panelColors) == panelEntryCount && std::size(hintLineColors) == hintLineEntryCount && std::size(keyBarColors) == keyBarEntryCount);

	std::array<TColorAttr, applicationEntryCount> entries{};
	// The groups this application never shows keep Turbo Vision's BIOS colours
	for (size_t i = 0; i < firstPanelApplicationEntry - 1; ++i)
		entries[i] = TColorAttr{ (uchar)stockApplicationColors[i] };

	const auto place = [&entries](size_t firstEntry, std::span<const TColorAttr> colors) {
		for (size_t i = 0; i < colors.size(); ++i)
			entries[firstEntry - 1 + i] = colors[i];
	};
	place(1, desktopColors);
	place(2, menuColors);
	place(8, blueWindowColors);
	place(32, grayDialogColors);
	place(firstPanelApplicationEntry, panelColors);
	place(firstHintLineApplicationEntry, hintLineColors);
	place(firstKeyBarApplicationEntry, keyBarColors);
	return TPalette{ entries.data(), (ushort)entries.size() };
}

// Maps a view's entries, in order, to consecutive application entries
template <size_t count>
[[nodiscard]] TPalette applicationEntriesPalette(uchar firstApplicationEntry)
{
	std::array<TColorAttr, count> entries{};
	for (size_t i = 0; i < count; ++i)
		entries[i] = TColorAttr{ (uchar)(firstApplicationEntry + i) };
	return TPalette{ entries.data(), (ushort)entries.size() };
}

} // namespace

const char* colourThemeMenuName(ColourTheme theme)
{
	return themes[(size_t)theme].menuName;
}

const char* colourThemeId(ColourTheme theme)
{
	return themes[(size_t)theme].id;
}

std::optional<ColourTheme> colourThemeWithId(std::string_view id)
{
	for (size_t i = 0; i < std::size(themes); ++i)
	{
		if (id == themes[i].id)
			return (ColourTheme)i;
	}
	return std::nullopt;
}

void applyColourTheme(ColourTheme theme)
{
	applicationPalette() = applicationPaletteFor(themes[(size_t)theme].colours);
}

TPalette& applicationPalette()
{
	static TPalette palette = applicationPaletteFor(themes[(size_t)ColourTheme::ClassicBlue].colours);
	return palette;
}

TPalette& hintLinePalette()
{
	static TPalette palette = applicationEntriesPalette<hintLineEntryCount>(firstHintLineApplicationEntry);
	return palette;
}

TPalette& keyBarPalette()
{
	static TPalette palette = applicationEntriesPalette<keyBarEntryCount>(firstKeyBarApplicationEntry);
	return palette;
}

TPalette& panelPalette()
{
	static TPalette palette = [] {
		static constexpr char blueWindowEntries[] = cpBlueWindow;
		static_assert(sizeof(blueWindowEntries) == ppFileListRow);
		std::array<TColorAttr, ppLastEntry> entries{};
		for (int entry = 1; entry <= ppLastEntry; ++entry)
			entries[(size_t)entry - 1] = TColorAttr{ entry < ppFileListRow ? (uchar)blueWindowEntries[entry - 1] : firstPanelApplicationEntry + entry - ppFileListRow };
		return TPalette{ entries.data(), (ushort)entries.size() };
	}();
	return palette;
}

#include "ctuipalette.h"
#include "ctuicolourtheme.h"

#define Uses_TApplication
#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <array>
#include <iterator>
#include <span>

namespace {

constexpr char stockApplicationColors[] = cpAppColor;
constexpr size_t panelEntryCount = ppLastEntry - ppFileListRow + 1, hintLineEntryCount = 6, keyBarEntryCount = 7;
// The panel's entries follow Turbo Vision's, then come the hint row's and the key bar's
constexpr uchar firstPanelApplicationEntry = sizeof(stockApplicationColors);
constexpr uchar firstHintLineApplicationEntry = firstPanelApplicationEntry + panelEntryCount;
constexpr uchar firstKeyBarApplicationEntry = firstHintLineApplicationEntry + hintLineEntryCount;
constexpr size_t applicationEntryCount = firstKeyBarApplicationEntry - 1 + keyBarEntryCount;

[[nodiscard]] TPalette applicationPaletteFor(const ThemeParameters& c)
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
		{ c.fileDetailText, c.panelBack },       // ppFileListRowDetails
		{ c.selectedText, c.panelBack },         // ppFileListSelectedRow
		{ c.fileText, c.rowBandBack },           // ppFileListBandRow
		{ c.fileDetailText, c.rowBandBack },     // ppFileListBandRowDetails
		{ c.selectedText, c.rowBandBack },       // ppFileListBandSelectedRow
		{ c.cursorText, c.cursorBack },          // ppFileListCursor
		{ c.cursorSelectedText, c.cursorBack },  // ppFileListSelectedCursor
		{ c.columnHeader, c.panelBack },         // ppColumnHeader
		{ c.statusText, c.panelBack },           // ppStatusText
		{ c.tabText, c.tabBack },                // ppActiveTab
		{ c.tabText, c.tabBack },                // ppFilterText
		{ c.frameActive, c.panelBack },          // ppFilterSelectedText
		{ c.frameIcons, c.tabBack },             // ppFilterArrows
		{ c.separator, c.panelBack },            // ppColumnSeparator
		{ c.separator, c.rowBandBack },          // ppBandColumnSeparator
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
		{ c.keyBarBack, c.keyBarBack },              // Background
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

int appliedRowsPerBand = colourThemes().front().parameters.rowsPerBand;

} // namespace

void applyColourTheme(const ColourTheme& theme)
{
	applicationPalette() = applicationPaletteFor(theme.parameters);
	appliedRowsPerBand = theme.parameters.rowsPerBand;
}

int fileListRowsPerBand()
{
	return appliedRowsPerBand;
}

TPalette& applicationPalette()
{
	static TPalette palette = applicationPaletteFor(colourThemes().front().parameters);
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

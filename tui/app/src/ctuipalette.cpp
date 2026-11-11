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

// All in the xterm 256-colour cube: a 256-colour terminal shows them exactly, and no terminal colour scheme redefines them
constexpr TColorRGB black{ 0x000000 }, navy{ 0x00005F }, red{ 0xAF0000 }, amber{ 0xD78700 }, cyan{ 0x00AFAF }, teal{ 0x008787 },
	steelBlue{ 0x005F87 }, darkGrey{ 0x5F5F5F }, grey{ 0xAFAFAF }, lightGreen{ 0x5FFF5F }, lightCyan{ 0x5FFFFF }, yellow{ 0xFFFF5F },
	white{ 0xFFFFFF };

// Entry 1: the desktop
constexpr TColorAttr desktopColors[]{
	{ navy, grey },
};

// Entries 2-7: the menus and the status lines
constexpr TColorAttr menuColors[]{
	{ black, grey },     // Normal text
	{ darkGrey, grey },  // Disabled text
	{ red, grey },       // Shortcut
	{ black, amber },    // Selected
	{ darkGrey, amber }, // Selected disabled
	{ navy, amber },     // Selected shortcut
};

// Entries 8-15: blue windows, the panels' type
constexpr TColorAttr blueWindowColors[]{
	{ grey, navy },       // Frame passive
	{ white, navy },      // Frame active
	{ lightGreen, navy }, // Frame icons
	{ navy, cyan },       // Scroll bar page area
	{ navy, cyan },       // Scroll bar controls
	{ yellow, navy },     // Scroller normal text
	{ navy, grey },       // Scroller selected text
	{ white, navy },      // Reserved
};

// Entries 32-63: gray dialogs, every dialog's type
constexpr TColorAttr grayDialogColors[]{
	{ black, grey },       // Frame passive
	{ white, grey },       // Frame active
	{ lightGreen, grey },  // Frame icons
	{ cyan, navy },        // Scroll bar page area
	{ cyan, navy },        // Scroll bar controls
	{ black, grey },       // Static text
	{ black, grey },       // Label normal
	{ white, grey },       // Label selected
	{ yellow, grey },      // Label shortcut
	{ black, amber },      // Button normal
	{ navy, amber },       // Button default
	{ white, amber },      // Button selected
	{ darkGrey, grey },    // Button disabled
	{ navy, amber },       // Button shortcut
	{ black, grey },       // Button shadow
	{ black, cyan },       // Cluster normal
	{ white, cyan },       // Cluster selected
	{ yellow, cyan },      // Cluster shortcut
	{ white, navy },       // Input line normal
	{ white, amber },      // Input line selected
	{ lightGreen, navy },  // Input line arrows
	{ black, amber },      // History arrow
	{ amber, grey },       // History sides
	{ navy, cyan },        // History window scroll bar page area
	{ navy, cyan },        // History window scroll bar controls
	{ black, cyan },       // List viewer normal
	{ white, amber },      // List viewer focused
	{ yellow, cyan },      // List viewer selected
	{ navy, cyan },        // List viewer divider
	{ cyan, navy },        // Info pane
	{ darkGrey, cyan },    // Cluster disabled
	0x00,                  // Reserved
};

// The panel's entries, in PanelPaletteEntry order
constexpr TColorAttr panelColors[]{
	{ lightCyan, navy },   // ppFileListRow
	{ yellow, navy },      // ppFileListSelectedRow
	{ black, teal },       // ppFileListCursor
	{ yellow, steelBlue }, // ppFileListSelectedCursor
	{ white, navy },       // ppColumnHeader
	{ grey, navy },        // ppStatusText
	{ black, cyan },       // ppActiveTab
	{ black, cyan },       // ppFilterText
	{ white, navy },       // ppFilterSelectedText
	{ lightGreen, cyan }   // ppFilterArrows
};
static_assert(std::size(menuColors) == 6 && std::size(blueWindowColors) == 8 && std::size(grayDialogColors) == 32);
static_assert(std::size(panelColors) == ppLastEntry - ppFileListRow + 1);

constexpr char stockApplicationColors[] = cpAppColor;
// The panel's entries follow Turbo Vision's
constexpr uchar firstPanelApplicationEntry = sizeof(stockApplicationColors);

} // namespace

TPalette& applicationPalette()
{
	static TPalette palette = [] {
		std::array<TColorAttr, firstPanelApplicationEntry - 1 + std::size(panelColors)> entries{};
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
		return TPalette{ entries.data(), (ushort)entries.size() };
	}();
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

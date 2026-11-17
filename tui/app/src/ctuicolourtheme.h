#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TColorAttr
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <span>

// A theme's colours by role, and the height of its row bands. The application palette pairs the colours into its entries.
struct ThemeParameters
{
	// Also the hint row's and the key bar's pressed items
	TColorRGB menuBack, menuText, menuShortcut, menuSelectedBack, menuSelectedText, menuSelectedShortcut;
	TColorRGB disabledText; // Everywhere

	TColorRGB panelBack;
	// The file list's rows lie in bands of rowsPerBand rows, every second band on rowBandBack. A theme without bands sets neither.
	TColorRGB rowBandBack = panelBack;
	int rowsPerBand = 1;
	TColorRGB framePassive, frameActive;
	TColorRGB frameIcons; // Also the arrows of input lines
	TColorRGB fileText;
	TColorRGB fileDetailText; // Every column but Name, in rows neither selected nor under the cursor
	TColorRGB selectedText, cursorBack, cursorText, cursorSelectedText, columnHeader, statusText, separator, cursorSeparator;
	TColorRGB tabBack, tabText; // The active tab and the filter line

	TColorRGB dialogBack, dialogText, dialogFrame, dialogShortcut;
	TColorRGB focusText; // The focused control's label, and the focused button or cluster item
	TColorRGB buttonBack, buttonText, buttonDefaultText, buttonShortcut, buttonShadow;
	TColorRGB controlBack, controlText; // Check boxes, radio buttons and lists
	TColorRGB inputBack, inputText, scrollBarText, scrollBarBack;
	TColorRGB selectionText; // On menuSelectedBack: an input line's selected text, a list's focused item, a pressed key

	TColorRGB bottomBack, hintText, hintKey, keyBack, keyText, keyLabelBack, keyLabelText;
	TColorRGB keyBarBack; // Between the key bar's items
};

struct ColourTheme
{
	enum class Polarity : uchar { Dark, Light };

	const char* id; // The settings store it: never changes
	const char* menuName; // The shortcut letter is between tildes
	Polarity polarity;
	ThemeParameters parameters;
};

// In the Theme menu's order, dark themes first. The first theme is the default.
[[nodiscard]] std::span<const ColourTheme> colourThemes();

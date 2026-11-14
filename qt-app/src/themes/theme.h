#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "theme/cbasepalette.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

class QApplication;

// One selectable look. `name` is unique within its polarity; a look retuned for the other polarity keeps its name.
struct Theme
{
	QString name;
	bool dark = false;
	CBasePalette palette;
};

// The first theme of each polarity is that polarity's default.
[[nodiscard]] const std::vector<Theme>& allThemes();

// Sets the application style, then applies the theme selected in CThemeController, now and whenever the selection changes.
// Call once, before any widget exists.
void installTheme(QApplication& app);

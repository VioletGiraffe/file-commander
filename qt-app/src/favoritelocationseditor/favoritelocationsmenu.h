#pragma once

#include "favoritelocationslist/cfavoritelocations.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>

class QMenu;
class QWidget;

// Adds an entry per favorite location and a submenu per category, each level with entries that add the current folder or a category,
// then an entry that opens the favorites editor.
// A location's path is resolved against currentFolderPosix; picking it calls goTo with the result. The current folder's entry is checked.
// dialogParent: the parent of the dialogs these entries open.
void fillFavoriteLocationsMenu(QMenu& menu, CFavoriteLocations& favorites, const QString& currentFolderPosix, QWidget& dialogParent, std::function<void(const QString& path)> goTo);

#pragma once

#include "favoritelocationslist/cfavoritelocations.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>

// Shows the favorites one group at a time, and edits them. currentFolder: the folder that Add folder adds.
// Returns the location chosen as stored, which may be relative or hold variables: see resolveUserPath(). Nullopt when closed without one.
[[nodiscard]] std::optional<QString> runFavoritesDialog(CFavoriteLocations& favorites, const QString& currentFolder);

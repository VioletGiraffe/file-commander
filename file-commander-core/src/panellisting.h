#pragma once

#include "detail/file_list_hashmap.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// The immediate children of dirPath, which ends with a separator, as a panel lists them: entries that are neither files
// nor directories (sockets) skipped, and the [..] entry added except at a root, whatever showHiddenFiles says.
// Empty when dirPath cannot be listed.
[[nodiscard]] FileListHashMap listDirectoryForPanel(const QString& dirPath, bool showHiddenFiles);

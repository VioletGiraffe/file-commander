#pragma once

#include "hashmap_helpers.h"
#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

using FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, IdentityHash>;

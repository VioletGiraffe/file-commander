#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

using FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, identity_hash>;

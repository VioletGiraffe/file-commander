#pragma once

#include "hashmap_helpers.h"
#include "cfilesystemobject.h"


#include <3rdparty/ankerl/unordered_dense.h>

using FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, IdentityHash>;

#pragma once

#include "3rdparty/ankerl/unordered_dense.h"
#include "hashmap_helpers.h"

#include "cfilesystemobject.h"

using FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, NullHash>;

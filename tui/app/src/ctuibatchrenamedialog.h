#pragma once

#include "cfilesystemobject.h"

#include <vector>

// Renames the items by rules, previewing every new name as the rules are edited, in the order given: the counter follows it
void runBatchRenameDialog(const std::vector<CFileSystemObject>& items);

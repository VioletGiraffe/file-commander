#pragma once

#include "cfilesystemobject.h"

#include <vector>

void scanDirectory(const CFileSystemObject& root, const std::function<void (const CFileSystemObject&)>& observer, const std::atomic<bool>& abort = std::atomic<bool>{false});

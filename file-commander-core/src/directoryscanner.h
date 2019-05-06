#pragma once

#include "cfilesystemobject.h"

#include <atomic>
#include <functional>

void scanDirectory(const CFileSystemObject& root, const std::function<void (const CFileSystemObject&)>& observer, const std::atomic<bool>& abort = std::atomic<bool>{false});

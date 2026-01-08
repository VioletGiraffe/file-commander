#pragma once

#include <atomic>
#include <functional>

class CFileSystemObject;

void scanDirectory(const CFileSystemObject& root, const std::function<void (const CFileSystemObject&)>& observer, const std::atomic<bool>& abort = std::atomic<bool>{false});

#pragma once

#include "detail/file_list_hashmap.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <functional>

// Depth-first scan; every discovered item is passed to the observer, with reachedThroughLink = true for items found by
// traversing a directory link (symlink / junction). With followDirLinks = false, a directory link is still reported
// as an item, but its contents are not.
// Link cycles are detected and not descended into, so the scan always terminates.
void scanDirectory(const CFileSystemObject& root,
	const std::function<void (const CFileSystemObject& item, bool reachedThroughLink)>& observer,
	const std::atomic<bool>& abort = std::atomic<bool>{false},
	bool followDirLinks = true);

// The immediate children of dirPath as a panel lists them: the [..] entry included except at the filesystem root, entries that
// are neither files nor directories (sockets) skipped.
[[nodiscard]] FileListHashMap listDirectoryForPanel(const QString& dirPath, bool showHiddenFiles);

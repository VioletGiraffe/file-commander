#pragma once

#include <atomic>
#include <functional>

class CFileSystemObject;

// Depth-first scan; every discovered item is passed to the observer, with reachedThroughLink = true for items found by
// traversing a directory link (symlink / junction). With followDirLinks = false, a directory link is still reported
// as an item, but its contents are not.
// Link cycles are detected and not descended into, so the scan always terminates.
void scanDirectory(const CFileSystemObject& root,
	const std::function<void (const CFileSystemObject& item, bool reachedThroughLink)>& observer,
	const std::atomic<bool>& abort = std::atomic<bool>{false},
	bool followDirLinks = true);

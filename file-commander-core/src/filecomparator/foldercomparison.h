#pragma once

#include "cfilesystemobject.h"

#include <atomic>
#include <functional>
#include <stdint.h>
#include <vector>

enum class EntryDifference {
	OnlyInLeft,
	OnlyInRight,
	Different,
	ComparisonFailed // Present on both sides, but unreadable, so equality could not be established either way
};

struct FolderComparisonEntry {
	QString relativePath; // Slash-separated, no leading or trailing slash; cased as found on disk
	EntryDifference status = EntryDifference::Different;
	// The side on which the entry is missing keeps UnknownType and a size of 0. Directories always report size 0.
	FileSystemObjectType leftType = UnknownType;
	FileSystemObjectType rightType = UnknownType;
	uint64_t leftSize = 0;
	uint64_t rightSize = 0;
};

struct FolderComparisonResult {
	std::vector<FolderComparisonEntry> differences; // Sorted by relativePath; identical entries are not listed
	uint64_t identicalFiles = 0; // Files proven byte-identical - the "N files verified" figure
	bool aborted = false;
};

// Recursive content-aware comparison of two directory trees. Entries are paired by relative path, and a pair is only
// read byte by byte when the cheap checks - entry type, then size - cannot already tell the two apart.
// Directory links are followed and files are compared through links, so a link is judged by the contents a transfer
// would actually copy.
// A directory present on one side only contributes an entry for itself and one for each of its descendants, leaving the
// caller free to collapse them for display.
// Progress covers the content-comparison phase only - enumeration runs first and has no denominator to report against.
FolderComparisonResult compareFolders(const QString& leftRoot, const QString& rightRoot,
	const std::atomic<bool>& abort = std::atomic<bool>{false}, const std::function<void (int percent)>& onProgress = {});

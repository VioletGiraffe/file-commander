#pragma once

#include "detail/file_list_hashmap.h"
#include "filesystemwatcher/cfilesystemwatcher.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "directoryscan/thinioqstring.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

// The immediate children of dirPath, which ends with a separator, as a panel lists them: entries that are neither files
// nor directories (sockets) skipped, and the [..] entry added except at a root, whatever showHiddenFiles says.
// nullopt when dirPath cannot be listed.
[[nodiscard]] std::optional<FileListHashMap> listDirectoryForPanel(const QString& dirPath, bool showHiddenFiles);

// A flattened listing holds the files of a folder tree, hidden ones only with showHiddenFiles
[[nodiscard]] bool isFlattenedListingItem(const CFileSystemObject& item, bool showHiddenFiles);
// Adds the flattened listing items under folderPath, following folder links. filesFound, when given, follows items.size().
// Returns whether the tree holds a link to a folder.
bool addFlattenedListingItems(const QString& folderPath, bool showHiddenFiles, FileListHashMap& items, const std::atomic<bool>& abort, std::atomic<size_t>* filesFound = nullptr);

// The current state of the paths a flattened listing's watch reported
struct FlattenedListingRecheck
{
	// A link to a folder appeared: only a new listing follows it as a listing would, breaking cycles through the folders above it.
	// The other fields are then incomplete.
	bool listAgain = false;
	FileListHashMap upserted; // The items at the paths, and under the folders that appeared at them
	std::vector<qulonglong> removedHashes; // Of the paths that hold no item now
	// The paths a folder may have left, each ending with a separator. A listed item under one is gone unless upserted has it.
	std::vector<QString> vanishedFolderCandidates;
};

[[nodiscard]] FlattenedListingRecheck recheckFlattenedListing(const std::vector<CFileSystemWatcher::ChangedPath>& changes, bool showHiddenFiles, const std::atomic<bool>& abort);
// Removes the items under any of folders, each ending with a separator, except those keep has. Returns the removed items' hashes.
[[nodiscard]] std::vector<qulonglong> removeItemsUnderFolders(FileListHashMap& items, std::vector<QString> folders, const FileListHashMap& keep);

// Checks a series of folders for being listable, skipping every folder under one already found unlistable:
// a check on an unreachable share or mount waits out a network timeout.
class ListableFolderProbe
{
public:
	// canList is replaceable for tests
	explicit ListableFolderProbe(const std::atomic<bool>& cancelled, std::function<bool(const QString&)> canList = &canListDirectory);

	// False without checking once cancelled
	[[nodiscard]] bool isListable(const QString& folder);
	// For a folder whose listing failed: records it as isListable would, without checking the folder again
	void addUnlistable(const QString& folder);

private:
	[[nodiscard]] bool isUnderUnlistableFolder(const QString& folder) const;

	const std::atomic<bool>& _cancelled;
	const std::function<bool(const QString&)> _canList;
	std::vector<QString> _unlistableFolders; // Each ends with a separator
};

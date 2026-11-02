#pragma once

#include "detail/file_list_hashmap.h"


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

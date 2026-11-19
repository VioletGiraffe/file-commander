#pragma once

#include "cpanel.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "foldercomparison/foldercomparison.h"
#include "threading/cbackgroundtask.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <optional>

// Compares two folders on a CBackgroundTask, which the constructor starts, for a UI that polls it
class CFolderComparison
{
public:
	struct EntryLocation
	{
		Panel panel;
		QString absolutePath;
	};

	// Both roots must be absolute
	CFolderComparison(QString leftRoot, QString rightRoot);

	[[nodiscard]] const QString& leftRoot() const noexcept { return _leftRoot; }
	[[nodiscard]] const QString& rightRoot() const noexcept { return _rightRoot; }

	// Of the file contents compared; nullopt while the folders are being scanned, which reports no progress
	[[nodiscard]] std::optional<int> percent() const noexcept;
	[[nodiscard]] bool finished() { return _task.finished(); }
	[[nodiscard]] const std::optional<FolderComparisonResult>& result() const noexcept { return _task.result(); }

	// The side that holds the entry: the right one for an entry only it has
	[[nodiscard]] EntryLocation locationOf(const FolderComparisonEntry& entry) const;

private:
	const QString _leftRoot, _rightRoot;
	std::atomic<int> _percent{ -1 };
	// Last: destroyed first, joining the comparison before the fields it reads and writes are gone
	CBackgroundTask<FolderComparisonResult> _task{ "Folder comparison" };
};

[[nodiscard]] QString folderComparisonStatusText(EntryDifference status);
[[nodiscard]] QString folderComparisonSummary(const FolderComparisonResult& result);

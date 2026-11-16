#pragma once

#include "cpanel.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "foldercomparison/foldercomparison.h"
#include "threading/cinterruptablethread.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <optional>

// Compares two folders on a worker thread, which the constructor starts, for a UI that polls it. Destruction cancels and joins.
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
	// Joins the worker once it has finished
	[[nodiscard]] bool finished();
	void cancel() noexcept;

	// Requires finished(). Empty if cancelled.
	[[nodiscard]] const std::optional<FolderComparisonResult>& result() const noexcept;
	// The side that holds the entry: the right one for an entry only it has
	[[nodiscard]] EntryLocation locationOf(const FolderComparisonEntry& entry) const;

private:
	const QString _leftRoot, _rightRoot;
	std::optional<FolderComparisonResult> _result; // Written by the worker before it sets _finished
	std::atomic<int> _percent{ -1 };
	std::atomic<bool> _finished{ false };
	// Last: destroyed first, joining the worker before the fields it writes are gone
	CInterruptableThread _thread{ "Folder comparison" };
};

[[nodiscard]] QString folderComparisonStatusText(EntryDifference status);
[[nodiscard]] QString folderComparisonSummary(const FolderComparisonResult& result);

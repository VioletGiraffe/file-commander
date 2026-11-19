#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "foldercomparison/filecontentcomparison.h"
#include "threading/cbackgroundtask.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <optional>

// Compares two files' contents on a CBackgroundTask, which the constructor starts, for a UI that polls it.
// Progress is a percentage: for a single file the denominator is unambiguously its size. Callers comparing many files
// should use compareFileContents() directly instead, so that progress can be aggregated over the whole batch.
class CFileComparator
{
public:
	CFileComparator(QString pathA, QString pathB);

	[[nodiscard]] int percent() const noexcept { return _percent; }
	[[nodiscard]] bool finished() { return _task.finished(); }
	// Requires finished(). Empty if cancelled.
	[[nodiscard]] const std::optional<ContentComparisonResult>& result() const noexcept { return _task.result(); }

private:
	std::atomic<int> _percent{ 0 };
	// Last: destroyed first, joining the comparison before the fields it writes are gone
	CBackgroundTask<ContentComparisonResult> _task{ "File comparison" };
};

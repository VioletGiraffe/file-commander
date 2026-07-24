#pragma once

#include "filecontentcomparison.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <functional>
#include <thread>

// Runs compareFileContents() on a worker thread, reporting progress as a percentage: for a single file the denominator
// is unambiguously its size. Callers comparing many files should use compareFileContents() directly instead, so that
// progress can be aggregated over the whole batch.
class CFileComparator
{
public:
	CFileComparator() noexcept = default;
	~CFileComparator() noexcept;

	CFileComparator(const CFileComparator&) = delete;
	CFileComparator& operator=(const CFileComparator&) = delete;

	void compareFilesThreaded(QString pathA, QString pathB, const std::function<void (int percent)>& progressCallback,
		const std::function<void (ContentComparisonResult)>& resultCallback);
	void abortComparison();

private:
	std::atomic<bool> _terminate {false};
	std::thread _comparisonThread;
};

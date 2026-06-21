#pragma once
#include "cfilesystemobject.h"

#include <atomic>
#include <stdint.h>
#include <vector>

struct FileStatistics
{
	inline bool operator==(const FileStatistics& other) const noexcept = default;
	[[nodiscard]] inline bool empty() const { return *this == FileStatistics{}; }

	std::vector<CFileSystemObject> largestFiles;

	uint64_t files = 0;
	uint64_t folders = 0;
	uint64_t occupiedSpace = 0;
};

// numThreads > 1 parallelizes the directory traversal across that many threads spun up just for this call (not a
// shared pool). Each thread accumulates into its own FileStatistics with no locking on the scanning hot path;
// the partial results are only merged into the final FileStatistics after every thread is done, on the calling thread.
// numThreads = 0 means automatic thread count calculation inside the function
[[nodiscard]] FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort = false, size_t numThreads = 0);

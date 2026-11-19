#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "threading/cbackgroundtask.h"


#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
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
// onScanningFolder is called with each folder as its scan starts, from the scan threads concurrently.
[[nodiscard]] FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort = false,
	const std::function<void(const QString& folder)>& onScanningFolder = {}, size_t numThreads = 0);

// calculateStatsFor on a CBackgroundTask, which the constructor starts, for a UI that polls it
class CFileStatisticsScan
{
public:
	struct Result
	{
		FileStatistics stats;
		uint64_t elapsedMs = 0;
	};

	explicit CFileStatisticsScan(std::vector<QString> paths);

	// Empty until the first folder
	[[nodiscard]] QString currentFolder() const;
	[[nodiscard]] bool finished() { return _task.finished(); }
	[[nodiscard]] const std::optional<Result>& result() const noexcept { return _task.result(); }

private:
	mutable std::mutex _currentFolderMutex;
	QString _currentFolder;
	// Last: destroyed first, joining the scan before the fields it writes are gone
	CBackgroundTask<Result> _task{ "File statistics" };
};

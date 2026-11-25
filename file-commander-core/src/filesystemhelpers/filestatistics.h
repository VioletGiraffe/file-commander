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

class CThreadPool;

struct FileStatistics
{
	inline bool operator==(const FileStatistics& other) const noexcept = default;
	[[nodiscard]] inline bool empty() const { return *this == FileStatistics{}; }

	std::vector<CFileSystemObject> largestFiles;

	uint64_t files = 0;
	uint64_t folders = 0;
	uint64_t occupiedSpace = 0;
};

// The workers of CController's statistics pool, which runs the Statistics command's scans and folder sizes
constexpr uint32_t StatisticsThreadPoolWorkers = 4;

// Scans on the calling thread and on every worker of pool. Each thread accumulates its own FileStatistics without locking;
// the calling thread merges them at the end.
// Holds pool's workers until the scan ends: work queued on pool behind it waits.
// May be called from one of pool's tasks: the calling thread can complete the scan alone.
// onScanningFolder is called with each folder as its scan starts, from the scanning threads concurrently.
// occupiedSpaceSoFar, if set, grows by each folder's files once that folder is listed, for a UI to show while the scan runs.
[[nodiscard]] FileStatistics calculateStatsFor(const std::vector<QString>& paths, CThreadPool& pool, const std::atomic_bool& abort = false,
	const std::function<void(const QString& folder)>& onScanningFolder = {}, std::atomic<uint64_t>* occupiedSpaceSoFar = nullptr);

// calculateStatsFor on a CBackgroundTask, which the constructor starts, for a UI that polls it
class CFileStatisticsScan
{
public:
	struct Result
	{
		FileStatistics stats;
		uint64_t elapsedMs = 0;
	};

	// pool must outlive the scan
	CFileStatisticsScan(std::vector<QString> paths, CThreadPool& pool);

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

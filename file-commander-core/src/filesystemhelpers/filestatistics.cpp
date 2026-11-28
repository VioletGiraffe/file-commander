#include "filestatistics.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "directoryscan/thinioqstring.h"
#include "threading/cthreadpool.h"
#include "timing/ctimeelapsed.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace {

// stats.largestFiles is sorted by size, largest first, and holds at most largestFilesCount files
// File: a FileSystemEntryView, or a CFileSystemObject for a root path
template <typename File>
void mergeFileIntoStats(FileStatistics& stats, const File& file, const size_t largestFilesCount)
{
	stats.occupiedSpace += file.size();
	++stats.files;

	auto& largestFiles = stats.largestFiles;
	if (largestFiles.size() == largestFilesCount)
	{
		if (largestFilesCount == 0 || file.size() <= largestFiles.back().size())
			return;
		largestFiles.pop_back();
	}

	const auto it = std::ranges::lower_bound(largestFiles, file.size(), std::greater{}, &CFileSystemObject::size);
	largestFiles.emplace(it, file);
}

// thin_io::entry_identity is a plain aggregate, so unordered_dense has no hash for it.
struct EntryIdentityHash
{
	using is_avalanching = void;

	[[nodiscard]] uint64_t operator()(const thin_io::entry_identity& id) const noexcept
	{
		static_assert(sizeof(id) == sizeof(id.filesystem) + sizeof(id.entry), "Padding would put indeterminate bytes into the hash");
		return ankerl::unordered_dense::detail::wyhash::hash(&id, sizeof(id));
	}
};

// Parallel breadth-first traversal: threads pull directories from a shared queue and push discovered
// subdirectories back onto it, so the fan-out isn't limited to the root's immediate children. Each thread
// accumulates into its own FileStatistics with no locking on the hot path - the caller merges the per-thread
// results once every thread has finished (see calculateStatsFor).
std::vector<FileStatistics> scanParallel(const std::vector<QString>& rootPaths, CThreadPool& pool, const size_t largestFilesCount, const std::atomic_bool& abort,
	const std::function<void(const QString& folder)>& onScanningFolder, std::atomic<uint64_t>* occupiedSpaceSoFar)
{
	std::vector<QString> dirsToScan;
	FileStatistics rootStats; // stats for the root entries themselves (passed directly in rootPaths, not discovered by scanning)

	// Identities (see resolvedObjectId) of directory link targets already queued for scanning: each target is counted
	// once, and link cycles can't keep the queue alive forever. Guarded by queueMutex once the worker threads are running.
	ankerl::unordered_dense::set<thin_io::entry_identity, EntryIdentityHash> queuedLinkTargets;

	for (const QString& path : rootPaths)
	{
		const CFileSystemObject rootItem(path);
		if (rootItem.isDir())
		{
			++rootStats.folders; // The root itself counts
			if (!rootItem.isLink())
				dirsToScan.push_back(rootItem.fullAbsolutePath());
			else if (const auto targetId = resolvedObjectId(path); targetId && queuedLinkTargets.insert(*targetId).second)
				dirsToScan.push_back(rootItem.fullAbsolutePath());
		}
		else if (rootItem.isFile())
			mergeFileIntoStats(rootStats, rootItem, largestFilesCount);
	}
	if (occupiedSpaceSoFar)
		occupiedSpaceSoFar->fetch_add(rootStats.occupiedSpace, std::memory_order_relaxed);

	std::mutex queueMutex;
	std::condition_variable queueCondition; // This is just for threads to sleep/wake up when there is not enough work for all threads and when new work appears. Could be busy wait instead.
	size_t outstandingItems = dirsToScan.size(); // directories queued or currently being processed by some thread
	// outstandingItems is required to know when the work is fully complete. 'dirsToScan' becoming empty is not an indicator - some thread might be still scanning and about to push new work.

	// A thread only waits while another one lists a folder, so the calling thread can complete the scan alone when the pool's workers are busy
	const size_t threadCount = pool.maxWorkersCount() + 1;
	std::vector<FileStatistics> threadResults(threadCount);
	pool.parallelFor(threadCount, [&](const size_t threadIndex) {
		FileStatistics& stats = threadResults[threadIndex];
		stats.largestFiles.reserve(largestFilesCount);
		for (;;)
		{
			QString dir;
			{
				std::unique_lock locker(queueMutex);
				queueCondition.wait(locker, [&] { return abort || outstandingItems == 0 || !dirsToScan.empty(); });

				if (abort || outstandingItems == 0)
					return;

				dir = std::move(dirsToScan.back());
				dirsToScan.pop_back();
			}

			if (onScanningFolder)
				onScanningFolder(dir);

			std::vector<QString> newDirs;
			const uint64_t occupiedSpaceBeforeDir = stats.occupiedSpace;
			if (const auto entries = listDirectoryWithDetails(dir))
			{
				for (const auto& entry : *entries)
				{
					const FileSystemEntryView item{ dir, entry };
					if (item.isFile())
						mergeFileIntoStats(stats, item, largestFilesCount);
					else if (item.isDir())
					{
						++stats.folders;
						QString path = item.fullAbsolutePath();
						if (!item.isLink())
							newDirs.push_back(std::move(path));
						// An unresolvable id means a broken link - not traversable
						else if (const auto targetId = resolvedObjectId(path); targetId)
						{
							std::lock_guard locker(queueMutex);
							if (queuedLinkTargets.insert(*targetId).second)
								newDirs.push_back(std::move(path));
						}
					}
				}
			}
			if (occupiedSpaceSoFar)
				occupiedSpaceSoFar->fetch_add(stats.occupiedSpace - occupiedSpaceBeforeDir, std::memory_order_relaxed);

			{
				std::lock_guard locker(queueMutex);
				outstandingItems += newDirs.size();
				--outstandingItems; // this directory is now fully processed
				for (auto& newDir : newDirs)
					dirsToScan.push_back(std::move(newDir));
			}
			queueCondition.notify_all();
		}
	});

	threadResults.push_back(std::move(rootStats));
	return threadResults;
}

}

FileStatistics calculateStatsFor(const std::vector<QString>& paths, CThreadPool& pool, const size_t largestFilesCount, const std::atomic_bool& abort,
	const std::function<void(const QString& folder)>& onScanningFolder, std::atomic<uint64_t>* occupiedSpaceSoFar)
{
	const auto partials = scanParallel(paths, pool, largestFilesCount, abort, onScanningFolder, occupiedSpaceSoFar);

	// Final aggregation happens here, on the calling thread, only after every scanning thread above has finished.
	FileStatistics merged;
	for (const auto& partial : partials)
	{
		merged.files += partial.files;
		merged.folders += partial.folders;
		merged.occupiedSpace += partial.occupiedSpace;
		merged.largestFiles.insert(merged.largestFiles.end(), partial.largestFiles.begin(), partial.largestFiles.end());
	}

	std::ranges::sort(merged.largestFiles, [](const CFileSystemObject& l, const CFileSystemObject& r) {
		return l.size() > r.size();
	});

	if (merged.largestFiles.size() > largestFilesCount)
		merged.largestFiles.resize(largestFilesCount);

	return merged;
}

CFileStatisticsScan::CFileStatisticsScan(std::vector<QString> paths, CThreadPool& pool)
{
	_task.start([this, paths{ std::move(paths) }, &pool](const std::atomic<bool>& cancelled) -> std::optional<Result> {
		const CTimeElapsed timer{ true };
		FileStatistics stats = calculateStatsFor(paths, pool, LargestFilesCount, cancelled, [this](const QString& folder) {
			std::lock_guard locker{ _currentFolderMutex };
			_currentFolder = folder;
		});

		if (cancelled)
			return std::nullopt;
		return Result{ .stats = std::move(stats), .elapsedMs = timer.elapsed() };
	});
}

QString CFileStatisticsScan::currentFolder() const
{
	std::lock_guard locker{ _currentFolderMutex };
	return _currentFolder;
}

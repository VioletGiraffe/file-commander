#include "filestatistics.h"
#include "../cfilesystemobject.h"
#include "../filesystemhelperfunctions.h"

#include <3rdparty/ankerl/unordered_dense.h>

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

constexpr size_t MaxLargeFiles = 500;

void mergeFileIntoStats(FileStatistics& stats, const CFileSystemObject& discoveredItem)
{
	const auto size = discoveredItem.size();
	stats.occupiedSpace += size;
	++stats.files;

	auto& largestFiles = stats.largestFiles;
	const auto nLargest = largestFiles.size();

	const auto it = std::ranges::lower_bound(largestFiles, discoveredItem, [](const CFileSystemObject& l, const CFileSystemObject& r) {
		return l.size() > r.size();
	});
	largestFiles.insert(it, discoveredItem);

	if (nLargest >= MaxLargeFiles)
		largestFiles.pop_back();
}

// Parallel breadth-first traversal: worker threads pull directories from a shared queue and push discovered
// subdirectories back onto it, so the fan-out isn't limited to the root's immediate children. Each thread
// accumulates into its own FileStatistics with no locking on the hot path - the caller merges the per-thread
// results once every thread has finished (see calculateStatsFor).
std::vector<FileStatistics> scanParallel(const std::vector<QString>& rootPaths, size_t numThreads, const std::atomic_bool& abort)
{
	std::vector<QString> dirsToScan;
	FileStatistics rootStats; // stats for the root entries themselves (passed directly in rootPaths, not discovered by scanning)

	// Identities (see resolvedObjectId) of directory link targets already queued for scanning: each target is counted
	// once, and link cycles can't keep the queue alive forever. Guarded by queueMutex once the worker threads are running.
	ankerl::unordered_dense::set<std::pair<uint64_t, uint64_t>> queuedLinkTargets;

	for (const QString& path : rootPaths)
	{
		const CFileSystemObject rootItem(path);
		if (rootItem.isDir())
		{
			++rootStats.folders; // scanDirectory() elsewhere in the codebase counts the root dir itself; stay consistent with that
			if (!rootItem.isLink())
				dirsToScan.push_back(path);
			else if (const auto targetId = resolvedObjectId(path); targetId && !queuedLinkTargets.contains(*targetId))
			{
				queuedLinkTargets.insert(*targetId);
				dirsToScan.push_back(path);
			}
		}
		else if (rootItem.isFile())
		{
			++rootStats.files;
			rootStats.occupiedSpace += rootItem.size();
		}
	}

	std::mutex queueMutex;
	std::condition_variable queueCondition; // This is just for threads to sleep/wake up when there is not enough work for all threads and when new work appears. Could be busy wait instead.
	size_t outstandingItems = dirsToScan.size(); // directories queued or currently being processed by some thread
	// outstandingItems is required to know when the work is fully complete. 'dirsToScan' becoming empty is not an indicator - some thread might be still scanning and about to push new work.

	std::vector<FileStatistics> threadResults(numThreads);
	std::vector<std::thread> threads;
	threads.reserve(numThreads);

	for (size_t i = 0; i < numThreads; ++i)
	{
		threads.emplace_back([&, i] {
			FileStatistics& stats = threadResults[i];
			stats.largestFiles.reserve(MaxLargeFiles + 1);
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

				const auto entries = QDir{dir}.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);

				std::vector<QString> newDirs;
				for (const auto& entry : entries)
				{
					const CFileSystemObject item(entry);
					if (item.isFile())
						mergeFileIntoStats(stats, item);
					else if (item.isDir())
					{
						++stats.folders;
						if (!item.isLink())
							newDirs.push_back(item.fullAbsolutePath());
						// An unresolvable id means a broken link - not traversable
						else if (const auto targetId = resolvedObjectId(item.fullAbsolutePath()); targetId)
						{
							std::lock_guard locker(queueMutex);
							if (!queuedLinkTargets.contains(*targetId))
							{
								queuedLinkTargets.insert(*targetId);
								newDirs.push_back(item.fullAbsolutePath());
							}
						}
					}
				}

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
	}

	for (auto& t : threads)
		t.join();

	threadResults.push_back(std::move(rootStats));
	return threadResults;
}

}

FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort, size_t numThreads)
{
	if (numThreads == 0)
		numThreads = qBound(1u, std::thread::hardware_concurrency(), 5u);

	const auto partials = scanParallel(paths, numThreads, abort);

	// Final aggregation happens here, on the calling thread, only after every worker thread above has finished.
	FileStatistics merged;
	merged.largestFiles.reserve(MaxLargeFiles + 1);
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

	if (merged.largestFiles.size() > MaxLargeFiles)
		merged.largestFiles.resize(MaxLargeFiles);

	return merged;
}

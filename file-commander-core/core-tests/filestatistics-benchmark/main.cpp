// Times calculateStatsFor (filesystemhelpers/filestatistics.cpp) and each part of its work, on generated folder trees.
// How to run it: doc/testing.md, "File statistics benchmark".
//
// Workloads, in the folders --generate creates:
//   tree   - a wide tree of many files: one Statistics or folder size scan of a large folder
//   small  - many folders without subfolders: every folder's size at once (CController::displayEachDirSize)
//   narrow - a chain of folders, each holding one subfolder: the scan threads mostly wait for work
// Cases:
//   part:*   - one thread lists every folder of the tree, adding each part of the per-entry work in turn
//   real:*   - calculateStatsFor itself
//   mirror:* - a copy of its scan with each suspected part replaceable, checked against calculateStatsFor before timing
//   team:*   - starting and joining calculateStatsFor's threads without any work

#include "cfilesystemobject.h"
#include "filesystemhelpers/filestatistics.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "directoryscan/thinioqstring.h"
#include "qt_paths.hpp" // thin_io
#include "threading/cthreadpool.h"
#include "threading/thread_helpers.h"
#include "utility/performance_cores.hpp"
#include "utility/power_throttling.hpp"

DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;

[[noreturn]] static void fail(const QString& message)
{
	fprintf(stderr, "%s\n", qUtf8Printable(message));
	exit(EXIT_FAILURE);
}

// ---------------------------------------------------------------- Generation

static void writeFile(const QString& path, const int size)
{
	QFile file{ path };
	if (!file.open(QFile::WriteOnly) || file.write(QByteArray(size, 'x')) != size)
		fail("Cannot write " + path);
}

static void makeFolder(const QString& path)
{
	if (!QDir{}.mkpath(path))
		fail("Cannot create " + path);
}

// Sizes stay below NTFS's limit for data held in the file record, so the files take no clusters
static void writeFiles(const QString& folder, const int count, std::mt19937& random)
{
	static constexpr const char* Extensions[]{ "txt", "cpp", "jpg", "dat", "h" };
	std::uniform_int_distribution<int> sizes{ 0, 600 };
	for (int i = 0; i < count; ++i)
		writeFile(folder % QStringLiteral("/file_%1.%2").arg(i, 5, 10, QChar{ '0' }).arg(Extensions[i % std::size(Extensions)]), sizes(random));
}

static void generate(const QString& root, const int filesPerLeaf)
{
	std::mt19937 random{ 1 };

	// 20 x 10 x 10 leaf folders, and 10 files in every folder above them
	for (int a = 0; a < 20; ++a)
	{
		const QString aPath = root % QStringLiteral("/tree/a%1").arg(a, 2, 10, QChar{ '0' });
		makeFolder(aPath);
		writeFiles(aPath, 10, random);
		for (int b = 0; b < 10; ++b)
		{
			const QString bPath = aPath % QStringLiteral("/b%1").arg(b);
			makeFolder(bPath);
			writeFiles(bPath, 10, random);
			for (int c = 0; c < 10; ++c)
			{
				const QString cPath = bPath % QStringLiteral("/c%1").arg(c);
				makeFolder(cPath);
				writeFiles(cPath, filesPerLeaf, random);
			}
		}
	}

	for (int i = 0; i < 1000; ++i)
	{
		const QString path = root % QStringLiteral("/small/s%1").arg(i, 4, 10, QChar{ '0' });
		makeFolder(path);
		writeFiles(path, 5, random);
	}

	QString path = root % QStringLiteral("/narrow");
	for (int depth = 0; depth < 60; ++depth, path += QStringLiteral("/d"))
	{
		makeFolder(path);
		writeFiles(path, 20, random);
	}
}

// ---------------------------------------------------------------- The scan's parts

constexpr size_t MaxLargeFiles = 500; // As in filestatistics.cpp

// The insertion mergeFileIntoStats in filestatistics.cpp does
static void insertIntoLargestFiles(std::vector<CFileSystemObject>& largestFiles, const CFileSystemObject& discoveredItem)
{
	const auto nLargest = largestFiles.size();

	const auto it = std::ranges::lower_bound(largestFiles, discoveredItem, [](const CFileSystemObject& l, const CFileSystemObject& r) {
		return l.size() > r.size();
	});
	largestFiles.insert(it, discoveredItem);

	if (nLargest >= MaxLargeFiles)
		largestFiles.pop_back();
}

[[nodiscard]] static bool isLargestFileCandidate(const std::vector<CFileSystemObject>& largestFiles, const uint64_t size)
{
	return largestFiles.size() < MaxLargeFiles || size > largestFiles.back().size();
}

// The kind and size CFileSystemObject::loadProperties derives from an entry
struct EntryClass
{
	bool isFile = false;
	bool isDir = false;
	bool isLink = false;
	uint64_t size = 0;
};

[[nodiscard]] static EntryClass classify(const thin_io::directory_entry& entry)
{
	const bool isLink = entry.attributes.is_link;
	const thin_io::entry_status& described = isLink && entry.link_target ? *entry.link_target : entry;
	switch (described.attributes.kind)
	{
	case thin_io::entry_kind::directory:
		return { .isDir = true, .isLink = isLink };
	case thin_io::entry_kind::regular_file:
		return { .isFile = true, .isLink = isLink, .size = described.logical_size.value_or(0) };
	default:
		return { .isFile = isLink, .isLink = isLink, .size = isLink ? described.logical_size.value_or(0) : 0 };
	}
}

struct EntryIdentityHash
{
	using is_avalanching = void;

	[[nodiscard]] uint64_t operator()(const thin_io::entry_identity& id) const noexcept
	{
		return ankerl::unordered_dense::detail::wyhash::hash(&id, sizeof(id));
	}
};

using IdentitySet = ankerl::unordered_dense::set<thin_io::entry_identity, EntryIdentityHash>;

// ---------------------------------------------------------------- The mirror of scanParallel

struct MirrorOptions
{
	// P3: a thread keeps one subfolder and queues the rest under one lock, waking only as many waiting threads as it queued folders
	bool leanQueue = false;
	// F1: the calling thread scans, and a helper thread starts only for a queued folder no thread waits for. Requires leanQueue.
	bool lazyTeam = false;
	// F2: an entry becomes a CFileSystemObject only as a largest-file candidate
	bool objectPerEntry = true;
	// F3: a file no larger than the smallest of the largest files is not inserted
	bool largestFilesEarlyOut = false;
	// A folder size has no use for the largest files
	bool largestFiles = true;
};

// The first entry is scanParallel as it is
static const std::vector<std::pair<const char*, MirrorOptions>> MirrorVariants{
	{ "mirror:current", {} },
	{ "mirror:P3-lean-queue", { .leanQueue = true } },
	{ "mirror:F3-early-out", { .largestFilesEarlyOut = true } },
	{ "mirror:F2-entries", { .objectPerEntry = false, .largestFilesEarlyOut = true } },
	{ "mirror:F2-no-largest", { .objectPerEntry = false, .largestFiles = false } },
	{ "mirror:all", { .leanQueue = true, .objectPerEntry = false, .largestFilesEarlyOut = true } },
};

static void scanFolder(const QString& dir, FileStatistics& stats, std::vector<QString>& newDirs, const MirrorOptions& options,
	std::mutex& queueMutex, IdentitySet& queuedLinkTargets)
{
	const auto queueFolderLink = [&](const QString& path) {
		// An unresolvable id means a broken link - not traversable
		if (const auto targetId = resolvedObjectId(path); targetId)
		{
			std::lock_guard locker(queueMutex);
			if (queuedLinkTargets.insert(*targetId).second)
				newDirs.push_back(path);
		}
	};

	const auto entries = listDirectoryWithDetails(dir);
	if (!entries)
		return;

	for (const auto& entry : *entries)
	{
		if (options.objectPerEntry)
		{
			const CFileSystemObject item{ dir, entry };
			if (item.isFile())
			{
				stats.occupiedSpace += item.size();
				++stats.files;
				if (options.largestFiles && (!options.largestFilesEarlyOut || isLargestFileCandidate(stats.largestFiles, item.size())))
					insertIntoLargestFiles(stats.largestFiles, item);
			}
			else if (item.isDir())
			{
				++stats.folders;
				if (!item.isLink())
					newDirs.push_back(item.fullAbsolutePath());
				else
					queueFolderLink(item.fullAbsolutePath());
			}
			continue;
		}

		const EntryClass entryClass = classify(entry);
		if (entryClass.isFile)
		{
			stats.occupiedSpace += entryClass.size;
			++stats.files;
			if (options.largestFiles && isLargestFileCandidate(stats.largestFiles, entryClass.size))
				insertIntoLargestFiles(stats.largestFiles, CFileSystemObject{ dir, entry });
		}
		else if (entryClass.isDir)
		{
			++stats.folders;
			QString path = dir % thin_io::to_qstring(entry.name) % u'/';
			if (!entryClass.isLink)
				newDirs.push_back(std::move(path));
			else
				queueFolderLink(path);
		}
	}
}

// One root folder, which must exist: the root's own handling is not under study
static FileStatistics mirrorScan(const QString& root, const size_t numThreads, const MirrorOptions& options, std::atomic<uint64_t>& occupiedSpaceSoFar)
{
	FileStatistics rootStats;
	rootStats.folders = 1;

	IdentitySet queuedLinkTargets;
	std::vector<QString> dirsToScan{ CFileSystemObject{ root }.fullAbsolutePath() };

	std::mutex queueMutex;
	std::condition_variable queueCondition;
	size_t outstandingItems = 1;
	size_t waitingThreads = 0; // Read by leanQueue only

	std::vector<FileStatistics> threadResults(numThreads);
	std::vector<std::thread> threads; // lazyTeam: guarded by queueMutex while threads run
	threads.reserve(numThreads);

	std::function<void(size_t)> work = [&](const size_t index) {
		FileStatistics& stats = threadResults[index];
		stats.largestFiles.reserve(MaxLargeFiles + 1);
		QString dir;
		bool holdsDir = false;
		for (;;)
		{
			if (!holdsDir)
			{
				std::unique_lock locker(queueMutex);
				++waitingThreads;
				queueCondition.wait(locker, [&] { return outstandingItems == 0 || !dirsToScan.empty(); });
				--waitingThreads;

				if (outstandingItems == 0)
					return;

				dir = std::move(dirsToScan.back());
				dirsToScan.pop_back();
			}
			holdsDir = false;

			std::vector<QString> newDirs;
			const uint64_t occupiedSpaceBeforeDir = stats.occupiedSpace;
			scanFolder(dir, stats, newDirs, options, queueMutex, queuedLinkTargets);
			occupiedSpaceSoFar.fetch_add(stats.occupiedSpace - occupiedSpaceBeforeDir, std::memory_order_relaxed);

			if (!options.leanQueue)
			{
				{
					std::lock_guard locker(queueMutex);
					outstandingItems += newDirs.size();
					--outstandingItems;
					for (auto& newDir : newDirs)
						dirsToScan.push_back(std::move(newDir));
				}
				queueCondition.notify_all();
				continue;
			}

			if (!newDirs.empty())
			{
				dir = std::move(newDirs.back());
				newDirs.pop_back();
				holdsDir = true;
			}

			size_t threadsToWake = 0;
			bool finished = false;
			{
				std::lock_guard locker(queueMutex);
				outstandingItems = outstandingItems + newDirs.size() + (holdsDir ? 1 : 0) - 1;
				for (auto& newDir : newDirs)
					dirsToScan.push_back(std::move(newDir));

				finished = outstandingItems == 0;
				threadsToWake = std::min(newDirs.size(), waitingThreads);
				// A helper per queued folder that no waiting thread takes
				for (size_t queued = dirsToScan.size(); options.lazyTeam && queued > waitingThreads && threads.size() + 1 < numThreads; --queued)
					threads.emplace_back(work, threads.size() + 1);
			}

			if (finished)
				queueCondition.notify_all();
			else
			{
				for (size_t i = 0; i < threadsToWake; ++i)
					queueCondition.notify_one();
			}
		}
	};

	if (options.lazyTeam)
		work(0);
	else
	{
		for (size_t i = 0; i < numThreads; ++i)
			threads.emplace_back(work, i);
	}

	for (auto& t : threads)
		t.join();

	threadResults.push_back(std::move(rootStats));
	FileStatistics merged;
	for (const auto& partial : threadResults)
	{
		merged.files += partial.files;
		merged.folders += partial.folders;
		merged.occupiedSpace += partial.occupiedSpace;
		merged.largestFiles.insert(merged.largestFiles.end(), partial.largestFiles.begin(), partial.largestFiles.end());
	}
	std::ranges::sort(merged.largestFiles, [](const CFileSystemObject& l, const CFileSystemObject& r) { return l.size() > r.size(); });
	if (merged.largestFiles.size() > MaxLargeFiles)
		merged.largestFiles.resize(MaxLargeFiles);
	return merged;
}

// ---------------------------------------------------------------- Measurement

[[nodiscard]] static size_t defaultThreadCount()
{
	return std::clamp(std::thread::hardware_concurrency(), 1u, 5u); // As calculateStatsFor chooses
}

// CController's application pool size: optimalCpuCount() in ccontroller.cpp
[[nodiscard]] static uint32_t applicationPoolSize()
{
	const auto cpu = CpuCount::get();
	uint32_t cores = std::max(cpu.performanceCoreCount(), cpu.efficiencyCoreCount());
	if (cores > 1 && cores == cpu.logicalProcessorCount())
		cores -= 1;
	return cores;
}

struct Case
{
	std::string name;
	std::function<uint64_t()> run; // Returns the files counted, which every case of a workload must agree on
};

// cv is the coefficient of variation of the times
static void measure(const QString& title, const std::vector<Case>& cases, const size_t warmupRounds, const size_t timedRounds)
{
	std::vector<uint64_t> files(cases.size());
	for (size_t round = 0; round < warmupRounds; ++round)
	{
		for (size_t i = 0; i < cases.size(); ++i)
			files[i] = cases[i].run();
	}

	std::vector<std::vector<double>> times(cases.size());
	for (size_t round = 0; round < timedRounds; ++round)
	{
		// Rotated, so that no case always runs right after the same other one
		for (size_t i = 0; i < cases.size(); ++i)
		{
			const size_t index = (i + round) % cases.size();
			const auto start = Clock::now();
			files[index] = cases[index].run();
			times[index].push_back(std::chrono::duration<double, std::milli>{ Clock::now() - start }.count());
		}
	}

	printf("%s\n  %-26s %9s %10s %10s %10s %6s\n", qUtf8Printable(title), "case", "files", "min ms", "median ms", "max ms", "cv %");
	for (size_t index = 0; index < cases.size(); ++index)
	{
		std::vector<double>& caseTimes = times[index];
		std::ranges::sort(caseTimes);
		const size_t middle = caseTimes.size() / 2;
		const double median = caseTimes.size() % 2 != 0 ? caseTimes[middle] : (caseTimes[middle - 1] + caseTimes[middle]) / 2.0;
		const double mean = std::accumulate(caseTimes.begin(), caseTimes.end(), 0.0) / (double)caseTimes.size();
		double squaredDeviations = 0.0;
		for (const double time : caseTimes)
			squaredDeviations += (time - mean) * (time - mean);
		const double deviation = caseTimes.size() > 1 ? std::sqrt(squaredDeviations / (double)(caseTimes.size() - 1)) : 0.0;

		printf("  %-26s %9llu %10.3f %10.3f %10.3f %6.1f\n", cases[index].name.c_str(), (unsigned long long)files[index], caseTimes.front(), median,
			caseTimes.back(), mean > 0.0 ? 100.0 * deviation / mean : 0.0);
	}
	fflush(stdout);
}

[[nodiscard]] static std::vector<uint64_t> largestFileSizes(const FileStatistics& stats)
{
	std::vector<uint64_t> sizes;
	for (const CFileSystemObject& file : stats.largestFiles)
		sizes.push_back(file.size());
	return sizes;
}

// Every mirror variant must find what calculateStatsFor finds; files of equal size can differ in the largest files
static void checkMirrorAgainstReal(const QString& root, const size_t numThreads)
{
	std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
	const FileStatistics real = calculateStatsFor({ root }, false, {}, &occupiedSpaceSoFar, numThreads);
	std::vector<std::pair<const char*, MirrorOptions>> variants = MirrorVariants;
	variants.emplace_back("mirror:F1-lazy-team", MirrorOptions{ .leanQueue = true, .lazyTeam = true });
	for (const auto& [name, options] : variants)
	{
		const FileStatistics mirrored = mirrorScan(root, numThreads, options, occupiedSpaceSoFar);
		const bool sameLargest = !options.largestFiles || largestFileSizes(mirrored) == largestFileSizes(real);
		if (mirrored.files != real.files || mirrored.folders != real.folders || mirrored.occupiedSpace != real.occupiedSpace || !sameLargest)
		{
			fail(QStringLiteral("%1 differs from calculateStatsFor on %2: %3 files, %4 folders, %5 bytes vs %6, %7, %8%9").arg(name, root)
				.arg(mirrored.files).arg(mirrored.folders).arg(mirrored.occupiedSpace).arg(real.files).arg(real.folders).arg(real.occupiedSpace)
				.arg(sameLargest ? "" : "; the largest files differ"));
		}
	}
}

// Every folder of the tree, root included, without following links
[[nodiscard]] static std::vector<QString> foldersOf(const QString& root)
{
	std::vector<QString> folders{ CFileSystemObject{ root }.fullAbsolutePath() };
	for (size_t i = 0; i < folders.size(); ++i)
	{
		const auto entries = listDirectoryWithDetails(folders[i]);
		if (!entries)
			continue;

		for (const auto& entry : *entries)
		{
			if (const EntryClass entryClass = classify(entry); entryClass.isDir && !entryClass.isLink)
				folders.push_back(folders[i] % thin_io::to_qstring(entry.name) % u'/');
		}
	}
	return folders;
}

static std::vector<Case> partCases(const QString& root)
{
	auto folders = std::make_shared<const std::vector<QString>>(foldersOf(root));
	return {
		{ "part:list", [folders] {
			uint64_t files = 0;
			for (const QString& folder : *folders)
			{
				for (const auto& entry : listDirectoryWithDetails(folder).value())
					files += classify(entry).isFile;
			}
			return files;
		} },
		{ "part:list+object", [folders] {
			uint64_t files = 0;
			for (const QString& folder : *folders)
			{
				for (const auto& entry : listDirectoryWithDetails(folder).value())
					files += CFileSystemObject{ folder, entry }.isFile();
			}
			return files;
		} },
		{ "part:list+object+largest", [folders] {
			FileStatistics stats;
			stats.largestFiles.reserve(MaxLargeFiles + 1);
			for (const QString& folder : *folders)
			{
				for (const auto& entry : listDirectoryWithDetails(folder).value())
				{
					const CFileSystemObject item{ folder, entry };
					if (item.isFile())
					{
						++stats.files;
						insertIntoLargestFiles(stats.largestFiles, item);
					}
				}
			}
			return stats.files;
		} },
	};
}

static Case realCase(const QString& root, const size_t numThreads)
{
	return { "real:" + std::to_string(numThreads) + "-threads", [root, numThreads] {
		std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
		return calculateStatsFor({ root }, false, {}, &occupiedSpaceSoFar, numThreads).files;
	} };
}

static Case mirrorCase(const QString& root, const size_t numThreads, std::string name, const MirrorOptions options)
{
	return { std::move(name), [root, numThreads, options] {
		std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
		return mirrorScan(root, numThreads, options, occupiedSpaceSoFar).files;
	} };
}

// The small folders' sizes, calculated as CPanel::displayDirSize does: each one a task on the application pool
static Case poolCase(CThreadPool& pool, std::shared_ptr<const std::vector<QString>> folders, std::string name, std::function<uint64_t(const QString&)> scan)
{
	return { std::move(name), [&pool, folders, scan] {
		std::atomic<uint64_t> files{ 0 };
		std::vector<std::future<void>> done;
		done.reserve(folders->size());
		for (const QString& folder : *folders)
			done.push_back(pool.enqueueWithFuture([&files, &folder, &scan] { files += scan(folder); }));
		for (auto& future : done)
			future.wait();
		return files.load();
	} };
}

static void measureWorkloads(const QString& root, const std::vector<size_t>& threadCounts, const size_t warmupRounds, const size_t timedRounds)
{
	const size_t numThreads = defaultThreadCount();
	const QString tree = root + QStringLiteral("/tree");
	const QString narrow = root + QStringLiteral("/narrow");

	checkMirrorAgainstReal(tree, numThreads);
	checkMirrorAgainstReal(narrow, numThreads);

	{
		std::vector<Case> cases = partCases(tree);
		for (const size_t count : threadCounts)
			cases.push_back(realCase(tree, count));
		for (const auto& [name, options] : MirrorVariants)
			cases.push_back(mirrorCase(tree, numThreads, name, options));
		measure(QStringLiteral("tree (mirror: %1 threads)").arg(numThreads), cases, warmupRounds, timedRounds);
	}

	{
		std::vector<Case> cases;
		for (const size_t count : threadCounts)
			cases.push_back(realCase(narrow, count));
		cases.push_back(mirrorCase(narrow, numThreads, "mirror:current", {}));
		cases.push_back(mirrorCase(narrow, numThreads, "mirror:P3-lean-queue", { .leanQueue = true }));
		cases.push_back(mirrorCase(narrow, numThreads, "mirror:F1-lazy-team", { .leanQueue = true, .lazyTeam = true }));
		measure(QStringLiteral("narrow (mirror: %1 threads)").arg(numThreads), cases, warmupRounds, timedRounds);
	}

	{
		std::vector<QString> smallFolders = foldersOf(root + QStringLiteral("/small"));
		smallFolders.erase(smallFolders.begin()); // The parent of the small folders
		for (const QString& folder : smallFolders)
			checkMirrorAgainstReal(folder, numThreads);

		const auto folders = std::make_shared<const std::vector<QString>>(std::move(smallFolders));
		const uint32_t poolSize = applicationPoolSize();
		CThreadPool pool{ poolSize, "Benchmark application pool" };
		pool.waitUntilStarted();

		const auto real = [](const size_t count) {
			return [count](const QString& folder) {
				std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
				return calculateStatsFor({ folder }, false, {}, &occupiedSpaceSoFar, count).files;
			};
		};
		const auto mirror = [numThreads](const MirrorOptions options) {
			return [numThreads, options](const QString& folder) {
				std::atomic<uint64_t> occupiedSpaceSoFar{ 0 };
				return mirrorScan(folder, numThreads, options, occupiedSpaceSoFar).files;
			};
		};

		std::vector<Case> cases;
		for (const size_t count : threadCounts)
			cases.push_back(poolCase(pool, folders, "real:" + std::to_string(count) + "-threads", real(count)));
		cases.push_back(poolCase(pool, folders, "mirror:current", mirror({})));
		cases.push_back(poolCase(pool, folders, "mirror:P3-lean-queue", mirror({ .leanQueue = true })));
		cases.push_back(poolCase(pool, folders, "mirror:F1-lazy-team", mirror({ .leanQueue = true, .lazyTeam = true })));
		cases.push_back(poolCase(pool, folders, "team:" + std::to_string(numThreads) + "-threads", [numThreads](const QString&) {
			std::vector<std::thread> threads;
			for (size_t i = 0; i < numThreads; ++i)
				threads.emplace_back([] {});
			for (auto& thread : threads)
				thread.join();
			return uint64_t{ 0 };
		}));
		measure(QStringLiteral("small: %1 folders on a %2-thread pool (mirror: %3 threads)").arg(folders->size()).arg(poolSize).arg(numThreads), cases,
			warmupRounds, timedRounds);
	}
}

[[nodiscard]] static size_t countOption(const QCommandLineParser& parser, const QCommandLineOption& option, const size_t minimum)
{
	bool isNumber = false;
	const size_t value = parser.value(option).toULongLong(&isNumber);
	if (!isNumber || value < minimum)
		fail(QString{ "--%1 must be a number of at least %2" }.arg(option.names().front()).arg(minimum));

	return value;
}

int main(int argc, char* argv[])
{
	disablePowerThrottling();
	const int pinnedProcessors = pinToPerformanceCores();

	QCoreApplication app{ argc, argv };

	QCommandLineParser parser;
	parser.setApplicationDescription("Times calculateStatsFor and the parts of its scan. See doc/testing.md, \"File statistics benchmark\".");
	parser.addHelpOption();
	parser.addPositionalArgument("root", "The folder --generate filled.", "root");

	const QCommandLineOption generateOption{ "generate", "Create the benchmark folders under <root> instead of measuring." };
	const QCommandLineOption filesOption{ "files-per-leaf", "With --generate, the files in each of the tree's 2000 deepest folders.", "count", "50" };
	const QCommandLineOption threadsOption{ "threads", "The thread counts calculateStatsFor is timed with.", "counts", "1,2,4" };
	const QCommandLineOption warmupOption{ "warmup", "Untimed rounds before the timed ones.", "count", "2" };
	const QCommandLineOption runsOption{ "runs", "Timed rounds.", "count", "9" };
	parser.addOptions({ generateOption, filesOption, threadsOption, warmupOption, runsOption });
	parser.process(app);

	const QStringList arguments = parser.positionalArguments();
	if (arguments.size() != 1)
		parser.showHelp(EXIT_FAILURE);

	const QString root = QDir{ arguments.front() }.absolutePath();
	if (parser.isSet(generateOption))
	{
		generate(root, (int)countOption(parser, filesOption, 1));
		return EXIT_SUCCESS;
	}

	std::vector<size_t> threadCounts;
	for (const QString& count : parser.value(threadsOption).split(',', Qt::SkipEmptyParts))
	{
		bool isNumber = false;
		threadCounts.push_back(count.toULongLong(&isNumber));
		if (!isNumber || threadCounts.back() == 0)
			fail("--threads takes positive numbers separated by commas");
	}

	printf("Logical processors: %u, pinned to: %d, calculateStatsFor's default threads: %zu\n", std::thread::hardware_concurrency(), pinnedProcessors,
		defaultThreadCount());
	measureWorkloads(root, threadCounts, countOption(parser, warmupOption, 0), countOption(parser, runsOption, 1));
	return EXIT_SUCCESS;
}

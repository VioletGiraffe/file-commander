// Times each candidate container for FileListHashMap on the work one folder listing puts it through.
// How to run it: doc/testing.md, "File list map benchmark".
//
// One listing, as the panel handles it, is the unit measured:
//   build   - every entry moved into a new map (listDirectoryForPanel)
//   destroy - the previous listing's map freed (CPanel::publishFileListIfCurrent)
//   copy    - every value copied into a vector (CPanelWidget::fillFromList, on the UI thread)
// Lookups by key are timed but left out of the total: they run once per user command, not once per entry.

#include "cfilesystemobject.h"
#include "detail/file_list_hashmap.h"

#include "cpu_pinning.hpp"


// Submodule includes
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_node_map.hpp>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHash>
#include <QString>
RESTORE_COMPILER_WARNINGS

#ifdef __APPLE__
#include <malloc/malloc.h>
#elif defined(__FreeBSD__)
#include <malloc_np.h>
#else
#include <malloc.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <new>
#include <numeric>
#include <optional>
#include <random>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

struct HeapCounts
{
	int64_t liveBytes = 0;
	int64_t peakBytes = 0;
	uint64_t allocations = 0;
};

// Off during the timed passes
bool heapCounting = false;
HeapCounts heapCounts;

[[nodiscard]] size_t allocationSize(void* block) noexcept
{
#ifdef _WIN32
	return ::_msize(block);
#elif defined(__APPLE__)
	return ::malloc_size(block);
#else
	return ::malloc_usable_size(block);
#endif
}

}

// Replaced so the heap figures count every container's own allocations; the array and sized forms forward to these.
// Paths are QString data, allocated by malloc, so they are not counted.
void* operator new(size_t size)
{
	void* block = ::malloc(size != 0 ? size : 1);
	if (!block)
		throw std::bad_alloc{};

	if (heapCounting)
	{
		heapCounts.liveBytes += (int64_t)allocationSize(block);
		heapCounts.peakBytes = std::max(heapCounts.peakBytes, heapCounts.liveBytes);
		++heapCounts.allocations;
	}

	return block;
}

void operator delete(void* block) noexcept
{
	if (block && heapCounting)
		heapCounts.liveBytes -= (int64_t)allocationSize(block);

	::free(block);
}

namespace {

// The interface every candidate is driven through; each wrapper reduces to the container's own calls once inlined.
template <typename Map>
class StdInterfaceMap
{
public:
	static constexpr bool isFileListHashMap = std::is_same_v<Map, FileListHashMap>;

	void reserve(size_t count) { _map.reserve(count); }
	void insert(uint64_t key, CFileSystemObject&& object) { _map.try_emplace(key, std::move(object)); }

	[[nodiscard]] const CFileSystemObject* find(uint64_t key) const
	{
		const auto it = _map.find(key);
		return it != _map.end() ? &it->second : nullptr;
	}

	template <typename Visitor>
	void forEachValue(Visitor&& visit) const
	{
		for (const auto& item : _map)
			visit(item.second);
	}

private:
	Map _map;
};

struct QHashKey
{
	bool operator==(const QHashKey&) const noexcept = default;

	uint64_t value;
};

// The identity, as IdentityHash: the key is a path hash already
[[nodiscard]] size_t qHash(QHashKey key, size_t /*seed*/) noexcept
{
	return (size_t)key.value;
}

class QHashMap
{
public:
	static constexpr bool isFileListHashMap = false;

	void reserve(size_t count) { _map.reserve((qsizetype)count); }
	void insert(uint64_t key, CFileSystemObject&& object) { _map.emplace(QHashKey{ key }, std::move(object)); }

	[[nodiscard]] const CFileSystemObject* find(uint64_t key) const
	{
		const auto it = _map.constFind(QHashKey{ key });
		return it != _map.cend() ? &it.value() : nullptr;
	}

	template <typename Visitor>
	void forEachValue(Visitor&& visit) const
	{
		for (const CFileSystemObject& value : _map)
			visit(value);
	}

private:
	QHash<QHashKey, CFileSystemObject> _map;
};

// Every sample covers at least this many entries, batching small maps: far above the clock's resolution
constexpr size_t minEntriesPerSample = 256 * 1024;
// Untimed rounds before the samples: the heap settles into the state repeated listings keep it in
constexpr size_t warmupRounds = 2;

struct Workload
{
	std::vector<CFileSystemObject> entries;
	// Every key, shuffled: a selection arrives in display order, unrelated to the keys
	std::vector<uint64_t> lookupKeys;
	// Every lookup pass over one map must reproduce this sum of the entries' sizes
	uint64_t sizeSum = 0;
	// Maps per sample
	size_t batch = 1;
};

// A folder's entries, built in memory: the keys are hashes of distinct paths, spread as uniformly as a real folder's
[[nodiscard]] std::vector<CFileSystemObject> folderEntries(size_t count)
{
	static constexpr const char* extensions[] = { "txt", "cpp", "h", "jpg", "png", "pdf", "docx", "zip", "exe", "dll", "json", "mp3" };
	static constexpr char nameChars[] = "abcdefghijklmnopqrstuvwxyz0123456789 _-";

	std::mt19937_64 rng{ count };
	std::uniform_int_distribution<size_t> nameLength{ 4, 32 };
	std::uniform_int_distribution<size_t> nameChar{ 0, std::size(nameChars) - 2 };
	std::uniform_int_distribution<size_t> extension{ 0, std::size(extensions) - 1 };

	const QString parent = QStringLiteral("C:/Users/benchmark/Documents/Projects/folder/");
	std::vector<CFileSystemObject> entries;
	entries.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		QString name;
		for (const size_t length = nameLength(rng); (size_t)name.size() < length;)
			name += QLatin1Char{ nameChars[nameChar(rng)] };

		// The index makes every path distinct
		name += QString::number(i);

		const bool isDir = rng() % 10 == 0;
		CFileSystemObjectProperties properties;
		properties.fullPath = parent + name;
		if (isDir)
			properties.fullPath += '/';
		else
		{
			properties.fullPath += '.';
			properties.fullPath += QLatin1StringView{ extensions[extension(rng)] };
		}

		properties.type = isDir ? Directory : File;
		properties.exists = true;
		properties.size = isDir ? 0 : rng() % (64 * 1024 * 1024);
		properties.modificationTime = (time_t)(1'600'000'000 + rng() % 100'000'000);
		properties.creationTime = properties.modificationTime;
		entries.emplace_back(std::move(properties));
	}

	return entries;
}

[[nodiscard]] Workload makeWorkload(size_t entryCount)
{
	Workload workload;
	workload.entries = folderEntries(entryCount);
	workload.batch = (minEntriesPerSample + entryCount - 1) / entryCount;

	workload.lookupKeys.reserve(entryCount);
	for (const CFileSystemObject& entry : workload.entries)
	{
		workload.lookupKeys.push_back(entry.hash());
		workload.sizeSum += entry.size();
	}

	std::ranges::shuffle(workload.lookupKeys, std::mt19937_64{ entryCount });
	return workload;
}

using Clock = std::chrono::steady_clock;

[[nodiscard]] double nanosecondsSince(Clock::time_point start) noexcept
{
	return std::chrono::duration<double, std::nano>{ Clock::now() - start }.count();
}

struct PhaseNs
{
	[[nodiscard]] double listingTotal() const noexcept { return build + destroy + copy; }

	double build = 0;
	double destroy = 0;
	double copy = 0;
	double lookup = 0;
};

uint64_t checksum = 0;

template <typename Map>
void build(Map& map, std::vector<CFileSystemObject>& source, bool reserve)
{
	if (reserve)
		map.reserve(source.size());

	for (CFileSystemObject& object : source)
	{
		const uint64_t key = object.hash();
		map.insert(key, std::move(object));
	}
}

// Each phase is timed across the whole batch of maps; the result is per entry
template <typename Map>
[[nodiscard]] PhaseNs timeSample(const Workload& workload, bool reserve)
{
	std::vector<std::vector<CFileSystemObject>> sources(workload.batch, workload.entries);
	std::vector<std::vector<CFileSystemObject>> rows(workload.batch);
	for (auto& batchRows : rows)
		batchRows.reserve(workload.entries.size());

	std::vector<Map> maps;
	maps.reserve(workload.batch);

	PhaseNs ns;
	auto start = Clock::now();
	for (auto& source : sources)
		build(maps.emplace_back(), source, reserve);
	ns.build = nanosecondsSince(start);

	start = Clock::now();
	for (size_t i = 0; i < maps.size(); ++i)
		maps[i].forEachValue([&batchRows = rows[i]](const CFileSystemObject& value) { batchRows.push_back(value); });
	ns.copy = nanosecondsSince(start);

	uint64_t sizeSum = 0;
	start = Clock::now();
	for (const Map& map : maps)
	{
		for (const uint64_t key : workload.lookupKeys)
		{
			if (const CFileSystemObject* object = map.find(key))
				sizeSum += object->size();
		}
	}
	ns.lookup = nanosecondsSince(start);

	start = Clock::now();
	maps.clear();
	ns.destroy = nanosecondsSince(start);

	assert_r(sizeSum == workload.sizeSum * workload.batch);
	assert_r(rows.back().size() == workload.entries.size());
	checksum += sizeSum + rows.back().size();

	const double entryCount = (double)(workload.batch * workload.entries.size());
	return { .build = ns.build / entryCount, .destroy = ns.destroy / entryCount, .copy = ns.copy / entryCount, .lookup = ns.lookup / entryCount };
}

// Counts the build only, so every block freed while counting was allocated while counting
template <typename Map>
[[nodiscard]] HeapCounts measureHeapUse(const Workload& workload, bool reserve)
{
	std::vector<CFileSystemObject> source = workload.entries;
	std::optional<Map> map;

	heapCounts = {};
	heapCounting = true;
	build(map.emplace(), source, reserve);
	heapCounting = false;

	return heapCounts;
}

struct Candidate
{
	// FileListHashMap as listDirectoryForPanel uses it
	[[nodiscard]] bool isCurrent() const noexcept { return isFileListHashMap && reserve; }

	const char* name;
	bool reserve;
	bool isFileListHashMap;
	PhaseNs (*timeSample)(const Workload&, bool reserve);
	HeapCounts (*measureHeapUse)(const Workload&, bool reserve);
};

template <typename Map>
void addCandidate(std::vector<Candidate>& candidates, const char* name)
{
	for (const bool reserve : { false, true })
		candidates.push_back({ name, reserve, Map::isFileListHashMap, &timeSample<Map>, &measureHeapUse<Map> });
}

[[nodiscard]] std::vector<Candidate> allCandidates()
{
	std::vector<Candidate> candidates;
	addCandidate<StdInterfaceMap<ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, IdentityHash>>>(candidates, "ankerl segmented_map");
	addCandidate<StdInterfaceMap<ankerl::unordered_dense::map<qulonglong, CFileSystemObject, IdentityHash>>>(candidates, "ankerl map");
	addCandidate<StdInterfaceMap<boost::unordered_flat_map<qulonglong, CFileSystemObject, IdentityHash>>>(candidates, "boost unordered_flat_map");
	addCandidate<StdInterfaceMap<boost::unordered_node_map<qulonglong, CFileSystemObject, IdentityHash>>>(candidates, "boost unordered_node_map");
	addCandidate<StdInterfaceMap<std::unordered_map<qulonglong, CFileSystemObject, IdentityHash>>>(candidates, "std::unordered_map");
	addCandidate<QHashMap>(candidates, "QHash");
	return candidates;
}

[[nodiscard]] double percentile(std::vector<double> values, double fraction)
{
	std::ranges::sort(values);
	return values[(size_t)std::lround(fraction * (double)(values.size() - 1))];
}

[[nodiscard]] double medianOf(const std::vector<PhaseNs>& samples, double PhaseNs::*phase)
{
	std::vector<double> values;
	values.reserve(samples.size());
	for (const PhaseNs& sample : samples)
		values.push_back(sample.*phase);

	return percentile(std::move(values), 0.5);
}

struct Result
{
	const Candidate* candidate;
	PhaseNs median;
	double total;
	double totalIqrPercent;
	HeapCounts heap;
};

struct FolderSizeResults
{
	size_t entryCount;
	size_t batch;
	// Fastest total first
	std::vector<Result> results;
};

[[nodiscard]] FolderSizeResults benchmarkFolderSize(size_t entryCount, size_t sampleCount, const std::vector<Candidate>& candidates)
{
	const Workload workload = makeWorkload(entryCount);

	std::vector<std::vector<PhaseNs>> samples(candidates.size());
	std::vector<size_t> order(candidates.size());
	std::iota(order.begin(), order.end(), size_t{ 0 });
	std::mt19937_64 orderRng{ entryCount };
	for (size_t round = 0; round < warmupRounds + sampleCount; ++round)
	{
		// Drift in the clock or the heap lands on a different candidate every round
		std::ranges::shuffle(order, orderRng);
		for (const size_t i : order)
		{
			const PhaseNs ns = candidates[i].timeSample(workload, candidates[i].reserve);
			if (round >= warmupRounds)
				samples[i].push_back(ns);
		}
	}

	FolderSizeResults folderSize{ entryCount, workload.batch, {} };
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		std::vector<double> totals;
		for (const PhaseNs& sample : samples[i])
			totals.push_back(sample.listingTotal());

		const PhaseNs median{ .build = medianOf(samples[i], &PhaseNs::build), .destroy = medianOf(samples[i], &PhaseNs::destroy),
			.copy = medianOf(samples[i], &PhaseNs::copy), .lookup = medianOf(samples[i], &PhaseNs::lookup) };
		const double total = percentile(totals, 0.5);
		const double totalIqrPercent = (percentile(totals, 0.75) - percentile(totals, 0.25)) / total * 100.0;
		folderSize.results.push_back({ &candidates[i], median, total, totalIqrPercent, candidates[i].measureHeapUse(workload, candidates[i].reserve) });
	}

	std::ranges::sort(folderSize.results, {}, &Result::total);
	return folderSize;
}

void printTable(const FolderSizeResults& folderSize, size_t sampleCount)
{
	const auto current = std::ranges::find_if(folderSize.results, [](const Result& result) { return result.candidate->isCurrent(); });

	printf("\n%zu entries: %zu maps per sample, %zu samples\n", folderSize.entryCount, folderSize.batch, sampleCount);
	printf("%-32s %-7s %8s %8s %8s %8s %6s %8s %8s %8s %8s %7s\n", "Container", "Reserve", "Build", "Destroy", "Copy", "Total", "IQR", "vs now", "Lookup", "Heap", "Peak", "Allocs");
	for (const Result& result : folderSize.results)
	{
		char name[64];
		snprintf(name, sizeof(name), "%s%s", result.candidate->name, result.candidate->isCurrent() ? " (now)" : "");

		char versusCurrent[16] = "-";
		if (current != folderSize.results.end())
			snprintf(versusCurrent, sizeof(versusCurrent), "%+.1f%%", (result.total / current->total - 1.0) * 100.0);

		const double entryCount = (double)folderSize.entryCount;
		printf("%-32s %-7s %8.2f %8.2f %8.2f %8.2f %5.1f%% %8s %8.2f %8.1f %8.1f %7llu\n", name, result.candidate->reserve ? "yes" : "no",
			result.median.build, result.median.destroy, result.median.copy, result.total, result.totalIqrPercent, versusCurrent, result.median.lookup,
			(double)result.heap.liveBytes / entryCount, (double)result.heap.peakBytes / entryCount, (unsigned long long)result.heap.allocations);
	}
}

// A gap no wider than either side's IQR does not rank the two
[[nodiscard]] bool withinNoise(const Result& a, const Result& b)
{
	return std::abs(b.total / a.total - 1.0) * 100.0 <= std::max(a.totalIqrPercent, b.totalIqrPercent);
}

[[nodiscard]] const Result& resultOf(const FolderSizeResults& folderSize, const Candidate& candidate)
{
	return *std::ranges::find_if(folderSize.results, [&candidate](const Result& result) { return result.candidate == &candidate; });
}

// The baseline is FileListHashMap in the same mode
void printSummaryTable(const std::vector<FolderSizeResults>& allResults, const std::vector<Candidate>& candidates, bool reserve)
{
	const auto baseline = std::ranges::find_if(candidates, [reserve](const Candidate& candidate) { return candidate.isFileListHashMap && candidate.reserve == reserve; });
	assert_and_return_r(baseline != candidates.end(), );

	printf("\n%-32s", reserve ? "With reserve" : "Without reserve");
	for (const FolderSizeResults& folderSize : allResults)
		printf(" %16zu", folderSize.entryCount);

	printf("\n");
	for (const Candidate& candidate : candidates)
	{
		if (candidate.reserve != reserve)
			continue;

		char name[64];
		snprintf(name, sizeof(name), "%s%s", candidate.name, &candidate == &*baseline ? " (now)" : "");
		printf("%-32s", name);
		for (const FolderSizeResults& folderSize : allResults)
		{
			const Result& result = resultOf(folderSize, candidate);
			const Result& baselineResult = resultOf(folderSize, *baseline);

			char cell[32];
			if (&result == &baselineResult)
				snprintf(cell, sizeof(cell), "%.1f", result.total);
			else
			{
				snprintf(cell, sizeof(cell), "%.1f (%+.0f%%%s)", result.total, (result.total / baselineResult.total - 1.0) * 100.0,
					withinNoise(result, baselineResult) ? "~" : "");
			}

			printf(" %16s", cell);
		}

		printf("\n");
	}
}

void printSummary(const std::vector<FolderSizeResults>& allResults, const std::vector<Candidate>& candidates)
{
	if (std::ranges::none_of(candidates, &Candidate::isFileListHashMap))
	{
		printf("\nNo summary: FileListHashMap is not among the candidates\n");
		return;
	}

	printf("\nSummary: total ns per entry; in brackets, against the (now) row; ~: within IQR\n");
	printSummaryTable(allResults, candidates, true);
	printSummaryTable(allResults, candidates, false);
}

}

int main(int argc, char* argv[])
{
	QCoreApplication app{ argc, argv };

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Times the candidate containers for the panel's file list. How to run it: doc/testing.md, \"File list map benchmark\"."));
	parser.addHelpOption();
	const QCommandLineOption samplesOption{ QStringLiteral("samples"), QStringLiteral("Timed samples per container and size."), QStringLiteral("count"), QStringLiteral("15") };
	const QCommandLineOption sizesOption{ QStringLiteral("sizes"), QStringLiteral("Comma-separated entry counts."), QStringLiteral("counts"), QStringLiteral("100,1000,10000,100000,1000000") };
	parser.addOption(samplesOption);
	parser.addOption(sizesOption);
	parser.process(app);

	bool ok = false;
	const size_t sampleCount = parser.value(samplesOption).toULongLong(&ok);
	if (!ok || sampleCount == 0)
	{
		fprintf(stderr, "Invalid --samples\n");
		return 1;
	}

	std::vector<size_t> entryCounts;
	for (const QString& value : parser.value(sizesOption).split(','))
	{
		const size_t count = value.toULongLong(&ok);
		if (!ok || count == 0)
		{
			fprintf(stderr, "Invalid --sizes entry: %s\n", qUtf8Printable(value));
			return 1;
		}

		entryCounts.push_back(count);
	}

#ifndef NDEBUG
	printf("Debug build: the timings mean nothing\n");
#endif
	pinToPerformanceCore();

	printf("sizeof(CFileSystemObject): %zu bytes\n", sizeof(CFileSystemObject));
	printf("Build, Destroy, Copy, Lookup: ns per entry, medians. Total: build + destroy + copy, the work of one listing.\n");
	printf("IQR: the total's spread, as a share of its median. vs now: the total against FileListHashMap as the panel uses it.\n");
	printf("Heap, Peak: bytes per entry the container holds after the build, and at most during it. Allocs: per map built.\n");

	const std::vector<Candidate> candidates = allCandidates();
	std::vector<FolderSizeResults> allResults;
	for (const size_t entryCount : entryCounts)
	{
		allResults.push_back(benchmarkFolderSize(entryCount, sampleCount, candidates));
		printTable(allResults.back(), sampleCount);
	}

	printSummary(allResults, candidates);
	printf("\nChecksum %llu\n", (unsigned long long)checksum);
	return 0;
}

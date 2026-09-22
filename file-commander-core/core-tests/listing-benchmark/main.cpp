// Times listing a folder by each approach the panel could use, and generates the folders to time it on.
// How to run it, warm and cold: doc/testing.md, "Listing benchmark".

#include "cfilesystemobject.h"
#include "directoryscanner.h"

#include "link_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "file.hpp" // thin_io
#include "fs.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash> // std::hash<QString>
#include <QProcess>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <malloc.h>
#include <sched.h>
#include <sys/resource.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <numeric>
#include <random>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <unordered_set>
#include <vector>

using Clock = std::chrono::steady_clock;

[[noreturn]] static void fail(const QString& message)
{
	fprintf(stderr, "%s\n", qUtf8Printable(message));
	exit(EXIT_FAILURE);
}

struct Folder
{
	QString path; // As a panel holds it: absolute, '/'-separated, with the trailing separator
#ifdef _WIN32
	std::wstring nativePath;
#else
	QByteArray nativePath;
#endif
};

static Folder folderToList(const QString& argument)
{
	const CFileSystemObject object{ argument };
	if (!object.isDir())
		fail("Not a folder: " + argument);

	Folder folder;
	folder.path = object.fullAbsolutePath();
#ifdef _WIN32
	folder.nativePath = folder.path.toStdWString();
#else
	folder.nativePath = QFile::encodeName(folder.path);
#endif
	return folder;
}

struct Sample
{
	size_t entries = 0;
	double microseconds = 0.0;
};

[[nodiscard]] static double microsecondsBetween(const Clock::time_point start, const Clock::time_point end)
{
	return std::chrono::duration<double, std::micro>{ end - start }.count();
}

// Every variant reads the clock before its listing is destroyed: freeing the listing is not measured.

// The filters of listDirectoryForPanel
static constexpr QDir::Filters PanelListingFilters = QDir::Dirs | QDir::Files | QDir::NoDot | QDir::Hidden | QDir::System;

// Sorted by name, as a QDir sorts by default
static Sample listWithQt(const Folder& folder)
{
	const auto start = Clock::now();
	const QFileInfoList entries = QDir{ folder.path }.entryInfoList(PanelListingFilters);
	const auto end = Clock::now();
	return { (size_t)entries.size(), microsecondsBetween(start, end) };
}

static Sample listWithQtUnsorted(const Folder& folder)
{
	const auto start = Clock::now();
	const QFileInfoList entries = QDir{ folder.path }.entryInfoList(PanelListingFilters, QDir::Unsorted);
	const auto end = Clock::now();
	return { (size_t)entries.size(), microsecondsBetween(start, end) };
}

// Lists no [..] entry, which the other variants include
static Sample listWithThinIo(const Folder& folder)
{
	const auto start = Clock::now();
	const auto entries = thin_io::list_directory(folder.nativePath.data());
	const auto end = Clock::now();
	if (!entries)
		fail("thin_io::list_directory failed on " + folder.path + ": " + QString::fromStdString(thin_io::format_filesystem_error(entries.error())));

	return { entries->size(), microsecondsBetween(start, end) };
}

static Sample listForPanel(const Folder& folder)
{
	const auto start = Clock::now();
	const FileListHashMap items = listDirectoryForPanel(folder.path, true);
	const auto end = Clock::now();
	return { items.size(), microsecondsBetween(start, end) };
}

struct Variant
{
	const char* name;
	Sample (*list)(const Folder&);
};

static constexpr Variant AllVariants[]{
	{ "qt", &listWithQt },
	{ "qt-unsorted", &listWithQtUnsorted },
	{ "thinio", &listWithThinIo },
	{ "panel", &listForPanel },
};

static std::vector<const Variant*> selectedVariants(const QString& names)
{
	std::vector<const Variant*> variants;
	for (const QString& name : names.split(',', Qt::SkipEmptyParts))
	{
		const auto variant = std::find_if(std::begin(AllVariants), std::end(AllVariants), [&name](const Variant& candidate) {
			return name == QLatin1StringView{ candidate.name };
		});
		if (variant == std::end(AllVariants))
			fail("Unknown variant: " + name);

		variants.push_back(variant);
	}

	if (variants.empty())
		fail("No variants selected");

	return variants;
}

// A performance core at full clock: an efficiency core or power throttling would make the runs incomparable.
static void pinToPerformanceCore()
{
#ifdef _WIN32
	ULONG length = 0;
	::GetSystemCpuSetInformation(nullptr, 0, &length, ::GetCurrentProcess(), 0);
	std::vector<uint8_t> buffer(length);
	if (length == 0 || !::GetSystemCpuSetInformation(reinterpret_cast<SYSTEM_CPU_SET_INFORMATION*>(buffer.data()), length, &length, ::GetCurrentProcess(), 0))
	{
		printf("Not pinned: no CPU set information\n");
		return;
	}

	// The last CPU of the highest efficiency class: a performance core, and far from CPU 0, which takes the most interrupts
	const SYSTEM_CPU_SET_INFORMATION* chosen = nullptr;
	for (ULONG offset = 0; offset < length;)
	{
		const auto* info = reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data() + offset);
		if (info->Type == CpuSetInformation && info->CpuSet.Group == 0 && (!chosen || info->CpuSet.EfficiencyClass >= chosen->CpuSet.EfficiencyClass))
			chosen = info;

		offset += info->Size;
	}

	if (!chosen || !::SetThreadAffinityMask(::GetCurrentThread(), KAFFINITY{ 1 } << chosen->CpuSet.LogicalProcessorIndex))
	{
		printf("Not pinned: no usable CPU\n");
		return;
	}

	::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	THREAD_POWER_THROTTLING_STATE throttling{};
	throttling.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
	throttling.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
	throttling.StateMask = 0;
	::SetThreadInformation(::GetCurrentThread(), ThreadPowerThrottling, &throttling, sizeof(throttling));

	printf("Pinned to CPU %u\n", (unsigned)chosen->CpuSet.LogicalProcessorIndex);
#elif defined(__linux__)
	cpu_set_t allowed;
	CPU_ZERO(&allowed);
	if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
	{
		printf("Not pinned: no affinity mask\n");
		return;
	}

	// The highest allowed CPU. Uniform cores make it as good as any; a hybrid x86 machine, where sysfs does not
	// expose which cores are the fast ones, needs taskset instead.
	int chosen = -1;
	for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
	{
		if (CPU_ISSET(cpu, &allowed))
			chosen = cpu;
	}

	cpu_set_t single;
	CPU_ZERO(&single);
	if (chosen >= 0)
		CPU_SET(chosen, &single);

	if (chosen < 0 || ::sched_setaffinity(0, sizeof(single), &single) != 0)
	{
		printf("Not pinned: no usable CPU\n");
		return;
	}

	// Fails without privileges, and only matters when something else wants the core
	(void)::setpriority(PRIO_PROCESS, 0, -19);

	printf("Pinned to CPU %d\n", chosen);
#endif
}

// Allocated blocks only: freed ones the allocator keeps, such as the listing's temporaries, are not counted.
// Negative where the allocator does not report it.
[[nodiscard]] static int64_t heapBytesAllocated()
{
#ifdef _WIN32
	// The CRT allocates from the process heap
	HEAP_SUMMARY summary{};
	summary.cb = sizeof(summary);
	if (!::HeapSummary(::GetProcessHeap(), 0, &summary))
		return 0;

	return (int64_t)summary.cbAllocated;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
	// The main arena only, and this thread is the only one allocating
	return (int64_t)::mallinfo2().uordblks;
#else
	return -1;
#endif
}

static void reportPanelListingMemory(const std::vector<Folder>& folders)
{
	if (heapBytesAllocated() < 0)
		return;

	for (const Folder& folder : folders)
	{
		const int64_t before = heapBytesAllocated();
		const FileListHashMap items = listDirectoryForPanel(folder.path, true);
		const int64_t held = heapBytesAllocated() - before;
		printf("%s: the panel listing holds %.0f bytes per entry\n", qUtf8Printable(folder.path), items.empty() ? 0.0 : (double)held / (double)items.size());
	}
}

// us/entry is from the median; cv is the coefficient of variation of the times
static void printReport(const QString& title, const std::vector<const Variant*>& variants, const std::vector<std::vector<Sample>>& samplesByVariant)
{
	printf("%s\n  %-12s %9s %10s %10s %9s %6s\n", qUtf8Printable(title), "variant", "entries", "min ms", "median ms", "us/entry", "cv %");
	for (size_t index = 0; index < variants.size(); ++index)
	{
		std::vector<double> times;
		for (const Sample& sample : samplesByVariant[index])
			times.push_back(sample.microseconds);

		std::sort(times.begin(), times.end());
		const size_t middle = times.size() / 2;
		const double median = times.size() % 2 != 0 ? times[middle] : (times[middle - 1] + times[middle]) / 2.0;

		const double mean = std::accumulate(times.begin(), times.end(), 0.0) / (double)times.size();
		double squaredDeviations = 0.0;
		for (const double time : times)
			squaredDeviations += (time - mean) * (time - mean);
		const double deviation = times.size() > 1 ? std::sqrt(squaredDeviations / (double)(times.size() - 1)) : 0.0;

		const size_t entries = samplesByVariant[index].back().entries;
		printf("  %-12s %9zu %10.3f %10.3f %9.3f %6.1f\n", variants[index]->name, entries, times.front() / 1000.0, median / 1000.0,
			entries != 0 ? median / (double)entries : 0.0, mean > 0.0 ? 100.0 * deviation / mean : 0.0);
	}
}

static void measureWarm(const Folder& folder, const std::vector<const Variant*>& variants, const size_t warmupRounds, const size_t timedRounds)
{
	for (size_t round = 0; round < warmupRounds; ++round)
	{
		for (const Variant* variant : variants)
			(void)variant->list(folder);
	}

	std::vector<std::vector<Sample>> samples(variants.size());
	for (size_t round = 0; round < timedRounds; ++round)
	{
		// Rotated, so that no variant always runs right after the same other one
		for (size_t i = 0; i < variants.size(); ++i)
		{
			const size_t index = (i + round) % variants.size();
			samples[index].push_back(variants[index]->list(folder));
		}
	}

	printReport(folder.path, variants, samples);
}

// Returns the volume root: the last line the command prints
[[nodiscard]] static QString remountVolume(const QStringList& command)
{
	QProcess process;
	process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	process.start(command.front(), command.mid(1));
	if (!process.waitForFinished(-1) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
		fail("The remount command failed: " + command.join(' '));

	const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
	if (lines.isEmpty())
		fail("The remount command printed no volume root: " + command.join(' '));

	return lines.back().trimmed();
}

// Every listing follows a remount, which discards the volume's cache.
// The folders are relative to the volume root.
static void measureCold(const QStringList& folderNames, const std::vector<const Variant*>& variants, const size_t rounds, const QStringList& remountCommand)
{
	// First-use initialization must not land on a sample: each variant lists a folder off the volume once
	const Folder unrelatedFolder = folderToList(QCoreApplication::applicationDirPath());
	for (const Variant* variant : variants)
		(void)variant->list(unrelatedFolder);

	struct Job
	{
		size_t folder;
		size_t variant;
	};

	std::vector<Job> jobs;
	for (size_t folder = 0; folder < (size_t)folderNames.size(); ++folder)
	{
		for (size_t variant = 0; variant < variants.size(); ++variant)
			jobs.push_back({ folder, variant });
	}

	std::vector<std::vector<std::vector<Sample>>> samples((size_t)folderNames.size(), std::vector<std::vector<Sample>>(variants.size()));
	std::mt19937 random{ std::random_device{}() };
	for (size_t round = 1; round <= rounds; ++round)
	{
		printf("Round %zu of %zu\n", round, rounds);
		// The drive's own cache survives a remount: a fixed order would favour the same jobs every round
		std::shuffle(jobs.begin(), jobs.end(), random);
		for (const Job& job : jobs)
		{
			const Folder folder = folderToList(QDir{ remountVolume(remountCommand) }.filePath(folderNames[(qsizetype)job.folder]));
			const Sample sample = variants[job.variant]->list(folder);
			samples[job.folder][job.variant].push_back(sample);
			printf("  %s %s: %zu entries, %.3f ms\n", qUtf8Printable(folderNames[(qsizetype)job.folder]), variants[job.variant]->name, sample.entries, sample.microseconds / 1000.0);
			// Into a pipe, stdout is fully buffered, and a cold run takes minutes
			fflush(stdout);
		}
	}

	for (size_t folder = 0; folder < (size_t)folderNames.size(); ++folder)
		printReport(folderNames[(qsizetype)folder], variants, samples[folder]);
}

// Names are unique case-insensitively, as NTFS requires
[[nodiscard]] static QString randomBaseName(std::mt19937& random)
{
	static constexpr char Alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
	std::uniform_int_distribution<size_t> length{ 4, 24 };
	std::uniform_int_distribution<size_t> character{ 0, sizeof(Alphabet) - 2 };

	QString name;
	for (size_t i = 0, n = length(random); i < n; ++i)
		name += QLatin1Char{ Alphabet[character(random)] };

	return name;
}

[[nodiscard]] static bool createEmptyFile(const QString& path)
{
	thin_io::file file;
#ifdef _WIN32
	return file.open(reinterpret_cast<const wchar_t*>(path.utf16()), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew);
#else
	return file.open(QFile::encodeName(path).constData(), thin_io::file::access_mode::Write, thin_io::file::open_disposition::CreateNew);
#endif
}

// Windows marks a hidden entry with an attribute, POSIX with a leading dot
[[nodiscard]] static bool createHiddenFile(const QString& folderPath, const QString& name)
{
#ifdef _WIN32
	const QString path = folderPath + name;
	return createEmptyFile(path) && ::SetFileAttributesW(reinterpret_cast<const wchar_t*>(path.utf16()), FILE_ATTRIBUTE_HIDDEN) != FALSE;
#else
	return createEmptyFile(folderPath + '.' + name);
#endif
}

// Empty files, as content does not affect a listing, with 6% subfolders, 3% hidden files, links and a name ending in a dot.
[[nodiscard]] static bool generateFolder(const QString& folderPath, const size_t entryCount, std::mt19937& random)
{
	if (!QDir{}.mkpath(folderPath))
		return false;

	static const QString Extensions[]{ "txt", "jpg", "png", "cpp", "h", "pdf", "zip", "json", "dll", "tar.gz", "" };
	std::uniform_int_distribution<size_t> extensionIndex{ 0, std::size(Extensions) - 1 };

	std::unordered_set<QString> usedFoldedNames;
	const auto uniqueName = [&](const QString& extension) {
		for (;;)
		{
			QString name = randomBaseName(random);
			if (!extension.isEmpty())
				name += '.' + extension;

			if (usedFoldedNames.insert(name.toLower()).second)
				return name;
		}
	};

	size_t created = 0;
	const auto countCreated = [&created] {
		if (++created % 10000 == 0)
			printf("  %zu entries\n", created);
	};

	const QString base = folderPath + '/';
	const size_t folderCount = std::max<size_t>(1, entryCount * 6 / 100);
	const size_t hiddenCount = entryCount * 3 / 100;
	static constexpr size_t SpecialEntryCount = 4; // The three links and the name ending in a dot
	const size_t regularCount = entryCount - folderCount - hiddenCount - SpecialEntryCount;

	QString firstFolder;
	for (size_t i = 0; i < folderCount; ++i)
	{
		const QString path = base + uniqueName({});
		if (!QDir{}.mkdir(path))
			return false;

		if (firstFolder.isEmpty())
			firstFolder = path;

		countCreated();
	}

	for (size_t i = 0; i < hiddenCount; ++i)
	{
		if (!createHiddenFile(base, uniqueName(Extensions[extensionIndex(random)])))
			return false;

		countCreated();
	}

	QString firstFile;
	for (size_t i = 0; i < regularCount; ++i)
	{
		const QString path = base + uniqueName(Extensions[extensionIndex(random)]);
		if (!createEmptyFile(path))
			return false;

		if (firstFile.isEmpty())
			firstFile = path;

		countCreated();
	}

	// A symlink needs elevation or Developer Mode on Windows; the folder is still worth listing without one
	bool linksCreated = createDirectoryLink(firstFolder, base + "link-to-folder");
	linksCreated = createDirectorySymlink(firstFolder, base + "symlink-to-folder") && linksCreated;
	linksCreated = createFileSymlink(firstFile, base + "symlink-to-file") && linksCreated;
	if (!linksCreated)
		printf("  Some links could not be created\n");

	// Win32 path parsing strips a trailing dot, so only a \\?\ path reaches this entry; thin_io uses one for an absolute path
	if (!createEmptyFile(base + "name-ending-in-a-dot."))
		return false;

	return true;
}

// Each size has its own seed, so a skipped folder leaves the others' names unchanged.
static void generate(const QString& root, const QString& entryCounts, const uint32_t seed)
{
	for (const QString& countText : entryCounts.split(',', Qt::SkipEmptyParts))
	{
		bool isNumber = false;
		const size_t entryCount = countText.toULongLong(&isNumber);
		if (!isNumber || entryCount < 100)
			fail("An entry count must be a number of at least 100: " + countText);

		const QString folderPath = QDir{ root }.absoluteFilePath("entries-" + countText);
		if (QFileInfo::exists(folderPath))
		{
			printf("%s exists, skipped\n", qUtf8Printable(folderPath));
			continue;
		}

		printf("Generating %s\n", qUtf8Printable(folderPath));
		std::mt19937 random{ seed + (uint32_t)entryCount };
		if (!generateFolder(folderPath, entryCount, random))
			fail("Failed to generate " + folderPath);
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
	QCoreApplication app{ argc, argv };

	QCommandLineParser parser;
	parser.setApplicationDescription("Times listing folders the ways the panel could list them. See doc/testing.md, \"Listing benchmark\".");
	parser.addHelpOption();
	parser.addPositionalArgument("folders", "The folders to list.", "folders...");

	const QCommandLineOption generateOption{ "generate", "Create the benchmark folders under <root> instead of measuring.", "root" };
	const QCommandLineOption entriesOption{ "entries", "The sizes of the folders --generate creates.", "counts", "1000,10000,100000" };
	const QCommandLineOption seedOption{ "seed", "The seed of the generated names.", "number", "1" };
	const QCommandLineOption variantsOption{ "variants", "The listings to time, of qt, qt-unsorted, thinio and panel.", "names", "qt,qt-unsorted,thinio,panel" };
	const QCommandLineOption warmupOption{ "warmup", "Untimed rounds before the timed ones.", "count", "2" };
	const QCommandLineOption runsOption{ "runs", "Timed rounds.", "count", "15" };
	const QCommandLineOption coldOption{ "cold", "Take <rounds> cold samples of every folder and variant, shuffled each round, instead of timing warm.", "rounds" };
	const QCommandLineOption remountOption{ "remount",
		"With --cold, the command run before every listing, one --remount per argument, the program first. It must remount the volume "
		"and print its root as the last line; the folders are relative to that root.", "argument" };
	parser.addOptions({ generateOption, entriesOption, seedOption, variantsOption, warmupOption, runsOption, coldOption, remountOption });
	parser.process(app);

	if (parser.isSet(generateOption))
	{
		generate(parser.value(generateOption), parser.value(entriesOption), (uint32_t)countOption(parser, seedOption, 0));
		return EXIT_SUCCESS;
	}

	const QStringList arguments = parser.positionalArguments();
	if (arguments.isEmpty())
		parser.showHelp(EXIT_FAILURE);

	const std::vector<const Variant*> variants = selectedVariants(parser.value(variantsOption));

	if (parser.isSet(coldOption) != parser.isSet(remountOption))
		fail("--cold and --remount go together");

	if (parser.isSet(coldOption))
	{
		const size_t rounds = countOption(parser, coldOption, 1);
		pinToPerformanceCore();
		measureCold(arguments, variants, rounds, parser.values(remountOption));
		return EXIT_SUCCESS;
	}

	std::vector<Folder> folders;
	for (const QString& argument : arguments)
		folders.push_back(folderToList(argument));

	const size_t warmupRounds = countOption(parser, warmupOption, 0);
	const size_t timedRounds = countOption(parser, runsOption, 1);

	pinToPerformanceCore();
	reportPanelListingMemory(folders);
	for (const Folder& folder : folders)
		measureWarm(folder, variants, warmupRounds, timedRounds);

	return EXIT_SUCCESS;
}

#include "cfilesearchengine.h"

#include "cfilesystemobject.h"
#include "filelist/filenamefilter.h"
#include "filesearchquery.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "directoryscan/directoryscanner.h"
#include "directoryscan/thinioqstring.h"
#include "file.hpp"
#include "paths/pathsyntax.h"
#include "qt_paths.hpp" // thin_io
#include "qtcore_helpers/qstring_hash.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "threading/cthreadpool.h"
#include "threading/thread_helpers.h"
#include "timing/ctimeelapsed.h"
#include "utility/on_scope_exit.hpp"
#include "utility_functions/memory_functions.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QByteArrayView>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <semaphore>
#include <stddef.h>
#include <thread>
#include <utility>

// How much of a file one pass examines
static constexpr uint64_t contentScanWindowSize = 1024 * 1024;
// The regex path decodes this much context on either side of a window: a match crossing the window's edge is found
// while it is no longer than this
static constexpr uint64_t contentScanMarginSize = 4 * 1024;

enum class ContentMatch
{
	No,
	Yes,
	// Undecided: the file could not be read, or the regex engine hit one of its limits and gave up part way.
	// Reporting either as No would be a guess.
	Inconclusive
};

[[nodiscard]] static ContentMatch fileContentsMatches(const QString& path, const QRegularExpression& regex, const QByteArray& memoryPattern, const std::atomic_bool& cancellationRequested)
{
	if (cancellationRequested)
		return ContentMatch::No;

	thin_io::file file;
	if (!file.open(path.toUtf8().constData(), thin_io::file::access_mode::Read)) [[unlikely]]
		return ContentMatch::Inconclusive; // Locked, forbidden, or gone: its contents are simply unknown

	const bool useRawMemoryPattern = !memoryPattern.isEmpty();

	// Distinguished from an empty file: value_or(0) here would report a file of unknown size as one known to hold nothing.
	const auto querySize = file.size();
	if (!querySize) [[unlikely]]
		return ContentMatch::Inconclusive;

	const uint64_t fileSize = *querySize;
	if (useRawMemoryPattern && fileSize < (uint64_t)memoryPattern.size()) [[unlikely]]
		return ContentMatch::No;
	if (fileSize == 0) [[unlikely]]
		return ContentMatch::No;
	if (cancellationRequested)
		return ContentMatch::No;

	static constexpr auto toBytePtr = [](const void* ptr) -> const std::byte* {
		return reinterpret_cast<const std::byte*>(ptr);
	};

	auto* mappedFile = toBytePtr(file.mmap(thin_io::file::mmap_access_mode::ReadOnly, 0, fileSize));
	if (!mappedFile) [[unlikely]]
	{
		assert_debug_only(mappedFile);
		return ContentMatch::Inconclusive;
	}

	if (useRawMemoryPattern) // Match the bytes directly - fast
	{
		// Searched in place: nothing here decodes or rewrites, so a window costs only the cancellation check it
		// permits. Consecutive windows overlap by one byte less than the pattern, leaving no gap an occurrence
		// could fall into; a window stays at least twice the pattern so the step cannot collapse toward 1.
		const uint64_t patternSize = (uint64_t)memoryPattern.size();
		const uint64_t windowSize = std::max(contentScanWindowSize, 2 * patternSize);

		for (uint64_t offset = 0; offset < fileSize; offset += windowSize - patternSize + 1)
		{
			if (cancellationRequested)
				return ContentMatch::No;

			const auto searchLength = std::min(fileSize - offset, windowSize);
			if (memfind(mappedFile + offset, searchLength, memoryPattern.constData(), memoryPattern.size()) != nullptr)
				return ContentMatch::Yes;

			if (offset + searchLength == fileSize)
				break; // Reached the end; stepping back by the overlap would only re-scan this window's tail
		}

		return ContentMatch::No;
	}

	// Match using regex - slow(er). Each window is decoded with its margins, and matching starts at the window:
	// - a match no longer than the margin that crosses the window's edge is found;
	// - ^ and \A match only at the file's start, and lookbehinds see into the left margin;
	// - a match counts only when it starts in the window, so one ending where the right margin is cut is longer than the margin.
	// Decoded straight from the mapping: a NUL byte becomes U+0000, which the regex engine matches like any other character.
	const auto* const fileBytes = reinterpret_cast<const char*>(mappedFile);
	const auto bytesBetween = [fileBytes](const uint64_t from, const uint64_t to) {
		return QByteArrayView{ fileBytes + from, (qsizetype)(to - from) };
	};

	bool anyWindowInconclusive = false;
	for (uint64_t windowStart = 0; windowStart < fileSize; windowStart += contentScanWindowSize)
	{
		if (cancellationRequested)
			return ContentMatch::No;

		const uint64_t windowEnd = std::min(fileSize, windowStart + contentScanWindowSize);
		const uint64_t subjectStart = windowStart - std::min(windowStart, contentScanMarginSize);
		const uint64_t subjectEnd = std::min(fileSize, windowEnd + contentScanMarginSize);

		// One decoder for the three parts: a character split by an edge decodes whole, in the part holding its last byte.
		// It skips a BOM only at the subject's start, which for the first window is the file's.
		QStringDecoder decoder{ QStringDecoder::Utf8 };
		QString subject = decoder(bytesBetween(subjectStart, windowStart));
		const qsizetype windowBegin = subject.size();
		subject += decoder(bytesBetween(windowStart, windowEnd));
		const qsizetype windowFinish = subject.size();
		subject += decoder(bytesBetween(windowEnd, subjectEnd));
		// Empty for a window holding only a BOM, or only the start of a character the file's end cuts off
		if (windowBegin == windowFinish)
			continue;

		const QRegularExpressionMatch match = regex.match(subject, windowBegin);
		if (match.hasMatch() && match.capturedStart() < windowFinish)
			return ContentMatch::Yes; // A later window deciding beats an earlier one giving up
		if (!match.isValid())
			anyWindowInconclusive = true; // Ran out of backtracking budget; "no match" here would be a guess
	}

	return anyWindowInconclusive ? ContentMatch::Inconclusive : ContentMatch::No;
}

namespace
{
struct ContentSearchContext
{
	CFileSearchEngine::FileSearchListener* listener;
	const QRegularExpression* regex;
	const QByteArray* plainText;
	const std::atomic_bool* cancellationRequested;
	std::counting_semaphore<>* availableTaskSlots = nullptr;
	// Written by the pool's threads, read once they have all been joined
	std::atomic<uint64_t> inconclusiveItems{ 0 };
};

[[nodiscard]] static bool acquireContentTaskSlotUnlessCancelled(std::counting_semaphore<>& availableTaskSlots, const std::atomic_bool& cancellationRequested)
{
	while (!cancellationRequested)
	{
		if (!availableTaskSlots.try_acquire_for(std::chrono::milliseconds{20}))
			continue;

		if (!cancellationRequested)
			return true;

		availableTaskSlots.release();
	}

	return false;
}

// The query's roots without repeats. Spellings of one root can differ by the trailing '/' and, on Windows, by letter case.
class SearchRoots final
{
public:
	explicit SearchRoots(const QStringList& roots)
	{
		for (const QString& root : roots)
		{
			QString path = normalizedAbsolutePath(root);
			QStringList& candidates = _pathsByCaseKey[caseKey(path)];
			if (std::ranges::none_of(candidates, [&path](const QString& candidate) { return isSameEntry(candidate, path); }))
			{
				candidates.push_back(std::move(path));
				_distinctRoots.push_back(root);
			}
		}
	}

	[[nodiscard]] const QStringList& distinct() const noexcept { return _distinctRoots; }

	[[nodiscard]] bool contains(const QString& entryPath) const
	{
		const QString path = normalizedAbsolutePath(entryPath);
		const auto candidates = _pathsByCaseKey.find(caseKey(path));
		return candidates != _pathsByCaseKey.end()
			&& std::ranges::any_of(candidates->second, [&path](const QString& candidate) { return isSameEntry(candidate, path); });
	}

private:
	[[nodiscard]] static QString caseKey(const QString& path)
	{
#ifdef _WIN32
		return path.toCaseFolded();
#else
		return path;
#endif
	}

	// Requires equal caseKey()
	[[nodiscard]] static bool isSameEntry(const QString& left, const QString& right)
	{
		if (left == right)
			return true;

#ifdef _WIN32
		// Only the filesystem decides: NTFS keeps apart names that case folding merges, such as k and the Kelvin sign, and a folder can be case-sensitive
		const auto leftId = resolvedObjectId(left);
		return leftId && leftId == resolvedObjectId(right);
#else
		return false;
#endif
	}

private:
	ankerl::unordered_dense::map<QString, QStringList, QStringHash> _pathsByCaseKey;
	QStringList _distinctRoots;
};
} // namespace

bool CFileSearchEngine::searchInProgress() const
{
	return _searchInProgress;
}

bool CFileSearchEngine::search(const FileSearchQuery& query, FileSearchListener* listener)
{
	if (searchInProgress() || query.roots.empty())
		return false;

	// The previous search reported completion before its thread finished unwinding, and start() requires a joined thread
	waitForSearchToFinish();

	_searchInProgress = true;
	_workerThread.start([this, query, listener](const std::atomic<bool>& cancellationRequested) {
		searchThread(query, listener, cancellationRequested);
	});

	return true;
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.requestCancellation();
}

void CFileSearchEngine::waitForSearchToFinish()
{
	_workerThread.join();
}

void CFileSearchEngine::notifySearchFinished(FileSearchListener* listener, SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed)
{
	_searchInProgress = false;
	listener->searchFinished(status, itemsScanned, inconclusiveItems, msElapsed);
}

void CFileSearchEngine::searchThread(const FileSearchQuery& query, FileSearchListener* listener, const std::atomic<bool>& cancellationRequested) noexcept
{
	::setThreadName("File search engine thread");

	uint64_t itemCounter = 0;
	CTimeElapsed timer;
	timer.start();

	const FileNameFilter nameFilter{ query.nameFilters, query.nameCaseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive };

	// Not set for an empty list: a FileNameFilter without filters matches every name
	std::function<bool (const thin_io::directory_entry&)> isFolderExcluded;
	if (!query.excludedFolders.isEmpty())
	{
		isFolderExcluded = [filter = FileNameFilter{ query.excludedFolders, Qt::CaseInsensitive }](const thin_io::directory_entry& entry) {
			return filter.matches(thin_io::to_qstring(entry.name));
		};
	}

	const bool searchByContents = !query.contents.isEmpty();
	// memfind can only look for the byte sequence as given, so case folding and word boundaries both need the regex
	// engine. (TODO: a case-insensitive memory search is possible, just not with the built-in functions.)
	const bool useRegexEngine = query.contentsIsRegex || !query.contentsCaseSensitive || query.contentsWholeWords;

	QRegularExpression fileContentsRegExp;
	QByteArray fileContentsPlainText;
	if (searchByContents)
	{
		if (useRegexEngine)
		{
			QString pattern = query.contentsIsRegex ? query.contents : QRegularExpression::escape(query.contents);
			// \b asserts a word/non-word transition, so a query whose own edge is punctuation would demand a word
			// character just outside the match; these lookarounds only require the absence of one. The group keeps
			// both applied to the whole pattern - without it, alternation would bind first.
			if (query.contentsWholeWords)
				pattern.prepend(QLatin1StringView{ "(?<!\\w)(?:" }).append(QLatin1StringView{ ")(?!\\w)" });

			fileContentsRegExp.setPattern(pattern);

			// \w and \b are ASCII-only without Unicode properties, which would make any non-ASCII letter a word
			// separator. The name filters need no such option: nothing can put \w or \b into one of their patterns.
			QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
			if (!query.contentsCaseSensitive)
				patternOptions |= QRegularExpression::CaseInsensitiveOption;

			fileContentsRegExp.setPatternOptions(patternOptions);

			if (!fileContentsRegExp.isValid())
			{
				notifySearchFinished(listener, SearchInvalidPattern, 0, 0, 0);
				return;
			}
		}
		else
			fileContentsPlainText = query.contents.toUtf8();
	}

	ContentSearchContext contentSearchContext{ listener, &fileContentsRegExp, &fileContentsPlainText, &cancellationRequested };
	std::unique_ptr<std::counting_semaphore<>> availableContentTaskSlots;
	ptrdiff_t contentTaskSlotCount = 0;
	std::unique_ptr<CThreadPool> contentSearchPool;

	// item: a CFileSystemObject or a FileSystemEntryView
	const auto examineItem = [&](const auto& item, const bool reachedThroughLink) {
		if (cancellationRequested)
			return;

		if (itemCounter % 128 == 0)
		{
			// No need to report every single item and waste CPU cycles
			listener->itemScanned(item.fullAbsolutePath());
		}

		++itemCounter;

		if (searchByContents && !item.isFile())
			return;

		// Only when the type matters: isBundle() reads the disk on macOS
		if (query.itemType != FileSearchQuery::ItemType::Any)
		{
			const bool isFolder = item.isDir() && !item.isBundle();
			if ((query.itemType == FileSearchQuery::ItemType::Files && isFolder) || (query.itemType == FileSearchQuery::ItemType::Folders && !isFolder))
				return;
		}

		if (!nameFilter.matches(item.fullName()))
			return;

		if (searchByContents)
		{
			if (cancellationRequested)
				return;

			if (!contentSearchPool)
			{
				static constexpr uint32_t maximumContentWorkers = 8;
				const uint32_t contentWorkerCount = std::clamp(std::thread::hardware_concurrency(), 1u, maximumContentWorkers);
				contentTaskSlotCount = (ptrdiff_t)contentWorkerCount * 2;
				availableContentTaskSlots = std::make_unique<std::counting_semaphore<>>(contentTaskSlotCount);
				contentSearchContext.availableTaskSlots = availableContentTaskSlots.get();
				contentSearchPool = std::make_unique<CThreadPool>(contentWorkerCount, "File search by contents thread pool");
			}

			if (!acquireContentTaskSlotUnlessCancelled(*contentSearchContext.availableTaskSlots, cancellationRequested))
				return;

			contentSearchPool->enqueue([file = CFileSystemObject{ item }, reachedThroughLink, &contentSearchContext] {
				EXEC_ON_SCOPE_EXIT([&contentSearchContext] { contentSearchContext.availableTaskSlots->release(); });
				if (*contentSearchContext.cancellationRequested)
					return;

				const ContentMatch contentMatch = fileContentsMatches(file.fullAbsolutePath(), *contentSearchContext.regex,
					*contentSearchContext.plainText, *contentSearchContext.cancellationRequested);
				if (*contentSearchContext.cancellationRequested)
					return;

				if (contentMatch == ContentMatch::Yes)
					contentSearchContext.listener->matchFound(file, reachedThroughLink);
				else if (contentMatch == ContentMatch::Inconclusive)
					contentSearchContext.inconclusiveItems.fetch_add(1, std::memory_order_relaxed);
			});
		}
		else
			listener->matchFound(CFileSystemObject{ item }, reachedThroughLink);
	};

	// Each distinct root is handled once: a folder by its own scan, anything else by examining it here. The other scans do not cover it again.
	// A root below another is not redundant: the higher scan misses it behind an excluded folder, or a folder that can be entered but not listed (POSIX mode --x, Windows traverse rights).
	const SearchRoots roots{ query.roots };
	QStringList rootFolders;
	bool anyRootIsNotFolder = false;
	for (const QString& root : roots.distinct())
	{
		const CFileSystemObject rootItem{ root };
		if (rootItem.exists() && !rootItem.isDir())
		{
			anyRootIsNotFolder = true;
			examineItem(rootItem, false);
		}
		else
			rootFolders.push_back(rootItem.fullAbsolutePath());
	}

	std::function<bool (const QString&)> isAnotherRoot;
	if (roots.distinct().size() > 1)
		isAnotherRoot = [&roots](const QString& dirPath) { return roots.contains(dirPath); };

	for (const QString& rootFolder : rootFolders)
	{
		scanDirectory(rootFolder, [&](const QString& parentPath, const thin_io::directory_entry& entry, const bool reachedThroughLink) {
			const FileSystemEntryView item{ parentPath, entry };
			if (!anyRootIsNotFolder || item.isDir() || !roots.contains(item.fullAbsolutePath()))
				examineItem(item, reachedThroughLink);
		}, cancellationRequested, isFolderExcluded, true, isAnotherRoot);
	}

	if (contentSearchPool)
	{
		// Every task holds a slot until it ends, so holding all of them means no task is queued or running.
		// finishAllThreads(true) would instead join the workers first and run the queued tail serially on this thread.
		for (ptrdiff_t i = 0; i < contentTaskSlotCount; ++i)
			availableContentTaskSlots->acquire();

		contentSearchPool->finishAllThreads(false);
	}

	// Every content task has been joined by now, so the count is final
	const auto elapsedMs = timer.elapsed();
	notifySearchFinished(listener, cancellationRequested ? SearchCancelled : SearchFinished, itemCounter,
		contentSearchContext.inconclusiveItems.load(std::memory_order_relaxed), elapsedMs);
}

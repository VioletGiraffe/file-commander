#include "cfilesearchengine.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "directoryscan/directoryscanner.h"
#include "file.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "threading/cthreadpool.h"
#include "threading/thread_helpers.h"
#include "timing/ctimeelapsed.h"
#include "utility/on_scope_exit.hpp"
#include "utility_functions/memory_functions.h"


DISABLE_COMPILER_WARNINGS
#include <QByteArrayView>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <semaphore>
#include <stddef.h>
#include <thread>

// The name filter language: literal text, '*' for any run of characters, '?' for exactly one. A leading '^' or a
// trailing '$' anchors that end of the match to the name's; an end left unanchored matches anywhere.
[[nodiscard]] static QString nameFilterToRegex(QStringView filter)
{
	const bool anchorStart = filter.startsWith('^');
	if (anchorStart)
		filter = filter.sliced(1);

	const bool anchorEnd = filter.endsWith('$');
	if (anchorEnd)
		filter.chop(1);

	// Escaping everything and restoring only our own two wildcards is what keeps '.', '+' and brackets literal.
	// Every '*' in the escaped text is preceded by the backslash that escaped it, so neither replacement can match
	// across two escape sequences.
	QString pattern = QRegularExpression::escape(filter);
	pattern.replace(QLatin1StringView{ "\\*" }, QLatin1StringView{ ".*" });
	pattern.replace(QLatin1StringView{ "\\?" }, QLatin1StringView{ "." });

	// \A and \z rather than ^ and $: '$' also matches immediately before a trailing newline, which a POSIX name may end with.
	if (anchorStart)
		pattern.prepend(QLatin1StringView{ "\\A" });
	if (anchorEnd)
		pattern.append(QLatin1StringView{ "\\z" });

	return pattern;
}

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
} // namespace

bool CFileSearchEngine::searchInProgress() const
{
	return _searchInProgress;
}

bool CFileSearchEngine::search(
	const QStringList& filters, bool subjectCaseSensitive,
	const QStringList& where,
	const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
	FileSearchListener* listener)
{
	if (searchInProgress() || where.empty())
		return false;

	// The previous search reported completion before its thread finished unwinding, and start() requires a joined thread
	waitForSearchToFinish();

	_searchInProgress = true;
	_workerThread.start([=, this](const std::atomic<bool>& cancellationRequested) {
		searchThread(filters, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive, contentsWholeWords, contentsIsRegex, listener, cancellationRequested);
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

void CFileSearchEngine::searchThread(
	const QStringList& filters, bool subjectCaseSensitive,
	const QStringList& where,
	const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
	FileSearchListener* listener, const std::atomic<bool>& cancellationRequested) noexcept
{
	::setThreadName("File search engine thread");

	uint64_t itemCounter = 0;
	CTimeElapsed timer;
	timer.start();

	// The filters are alternatives, so one that matches every name makes name matching redundant altogether.
	const bool noFileNameFilter = filters.isEmpty() || std::ranges::any_of(filters, [](const QString& filter) { return filter == '*'; });

	std::vector<QRegularExpression> filterExpressions;
	if (!noFileNameFilter)
	{
		// Our wildcards stand for any character, including the newline a POSIX name may contain.
		QRegularExpression::PatternOptions patternOptions = QRegularExpression::DotMatchesEverythingOption;
		if (!subjectCaseSensitive)
			patternOptions |= QRegularExpression::CaseInsensitiveOption;

		filterExpressions.reserve(filters.size());
		for (const QString& filterString : filters)
			filterExpressions.emplace_back(nameFilterToRegex(filterString), patternOptions);
	}

	const bool searchByContents = !contentsToFind.isEmpty();
	// memfind can only look for the byte sequence as given, so case folding and word boundaries both need the regex
	// engine. (TODO: a case-insensitive memory search is possible, just not with the built-in functions.)
	const bool useRegexEngine = contentsIsRegex || !contentsCaseSensitive || contentsWholeWords;

	QRegularExpression fileContentsRegExp;
	QByteArray fileContentsPlainText;
	if (searchByContents)
	{
		if (useRegexEngine)
		{
			QString pattern = contentsIsRegex ? contentsToFind : QRegularExpression::escape(contentsToFind);
			// \b asserts a word/non-word transition, so a query whose own edge is punctuation would demand a word
			// character just outside the match; these lookarounds only require the absence of one. The group keeps
			// both applied to the whole pattern - without it, alternation would bind first.
			if (contentsWholeWords)
				pattern.prepend(QLatin1StringView{ "(?<!\\w)(?:" }).append(QLatin1StringView{ ")(?!\\w)" });

			fileContentsRegExp.setPattern(pattern);

			// \w and \b are ASCII-only without Unicode properties, which would make any non-ASCII letter a word
			// separator. The name filters need no such option: nothing can put \w or \b into one of their patterns.
			QRegularExpression::PatternOptions patternOptions = QRegularExpression::UseUnicodePropertiesOption;
			if (!contentsCaseSensitive)
				patternOptions |= QRegularExpression::CaseInsensitiveOption;

			fileContentsRegExp.setPatternOptions(patternOptions);

			if (!fileContentsRegExp.isValid())
			{
				notifySearchFinished(listener, SearchInvalidPattern, 0, 0, 0);
				return;
			}
		}
		else
			fileContentsPlainText = contentsToFind.toUtf8();
	}

	ContentSearchContext contentSearchContext{ listener, &fileContentsRegExp, &fileContentsPlainText, &cancellationRequested };
	std::unique_ptr<std::counting_semaphore<>> availableContentTaskSlots;
	ptrdiff_t contentTaskSlotCount = 0;
	std::unique_ptr<CThreadPool> contentSearchPool;

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn).fullAbsolutePath(),
			[&](const QString& parentPath, const thin_io::directory_entry& entry, bool reachedThroughLink) {
				if (cancellationRequested)
					return;

				const CFileSystemObject item{ parentPath, entry };

				if (itemCounter % 128 == 0)
				{
					// No need to report every single item and waste CPU cycles
					listener->itemScanned(item.fullAbsolutePath());
				}

				++itemCounter;

				if (searchByContents && !item.isFile())
					return;

				bool nameMatches = noFileNameFilter;
				if (!noFileNameFilter)
				{
					for (const auto& regexp : filterExpressions)
					{
						if (regexp.matchView(item.fullName()).hasMatch())
						{
							nameMatches = true;
							break;
						}
					}
				}

				if (!nameMatches)
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

					contentSearchPool->enqueue([item, reachedThroughLink, &contentSearchContext] {
						EXEC_ON_SCOPE_EXIT([&contentSearchContext] { contentSearchContext.availableTaskSlots->release(); });
						if (*contentSearchContext.cancellationRequested)
							return;

						const ContentMatch contentMatch = fileContentsMatches(item.fullAbsolutePath(), *contentSearchContext.regex,
							*contentSearchContext.plainText, *contentSearchContext.cancellationRequested);
						if (*contentSearchContext.cancellationRequested)
							return;

						if (contentMatch == ContentMatch::Yes)
							contentSearchContext.listener->matchFound(item, reachedThroughLink);
						else if (contentMatch == ContentMatch::Inconclusive)
							contentSearchContext.inconclusiveItems.fetch_add(1, std::memory_order_relaxed);
					});
				}
				else
					listener->matchFound(item, reachedThroughLink);

			}, cancellationRequested);
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

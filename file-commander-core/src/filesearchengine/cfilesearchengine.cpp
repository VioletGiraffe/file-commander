#include "cfilesearchengine.h"
#include "cfilesystemobject.h"
#include "timing/ctimeelapsed.h"
#include "directoryscanner.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "file.hpp"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "threading/thread_helpers.h"
#include "threading/cthreadpool.h"
#include "utility/on_scope_exit.hpp"
#include "utility_functions/memory_functions.h"

DISABLE_COMPILER_WARNINGS
#include <QRegularExpression>
#include <QStringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <semaphore>
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

#ifndef __ARM_ARCH_ISA_A64

#include <smmintrin.h>  // SSE4.1

// array is 4k in size and 4k aligned, so buffer overrun by the tail of <16 bytes is allowed.
inline void replace_null(std::byte* array, size_t size) noexcept
{
	const __m128i old_sse = _mm_set1_epi8(0);
	const __m128i new_sse = _mm_set1_epi8(' ');

	for (size_t i = 0; i < size; i += 16)
	{
		__m128i data = _mm_loadu_si128(reinterpret_cast<__m128i*>(array + i));  // Load 16 bytes
		__m128i mask = _mm_cmpeq_epi8(data, old_sse);           // Compare with old_value
		__m128i result = _mm_blendv_epi8(data, new_sse, mask);  // Blend new_value where mask is true
		_mm_storeu_si128(reinterpret_cast<__m128i*>(array + i), result); // Store the result back
	}
}

#else // ARM64

#include <arm_neon.h>

// array is 4k in size and 4k aligned, so buffer overrun by the tail of <16 bytes is allowed.
inline void replace_null(std::byte* array, size_t size)
{
	uint8x16_t old_neon = vdupq_n_u8(0);  // Duplicate old_value across all 16 bytes in the vector
	uint8x16_t new_neon = vdupq_n_u8(' ');  // Duplicate new_value across all 16 bytes in the vector

	for (size_t i = 0; i < size; i += 16)
	{
		uint8x16_t data = vld1q_u8(reinterpret_cast<const uint8_t*>(&array[i]));           // Load 16 bytes
		uint8x16_t mask = vceqq_u8(data, old_neon);      // Compare with old_value
		uint8x16_t result = vbslq_u8(mask, new_neon, data);  // Select new_value where mask is true, else original value
		vst1q_u8(reinterpret_cast<uint8_t*>(&array[i]), result);                     // Store the result back
	}
}
#endif

[[nodiscard]] static bool fileContentsMatches(const QString& path, const QRegularExpression& regex, const QByteArray& memoryPattern, const std::atomic_bool& cancellationRequested)
{
	if (cancellationRequested)
		return false;

	thin_io::file file;
	if (!file.open(path.toUtf8().constData(), thin_io::file::access_mode::Read)) [[unlikely]]
		return false;

	const bool useRawMemoryPattern = !memoryPattern.isEmpty();

	const uint64_t fileSize = file.size().value_or(0);
	if (useRawMemoryPattern && fileSize < (uint64_t)memoryPattern.size()) [[unlikely]]
		return false;
	if (fileSize == 0) [[unlikely]]
		return false;
	if (cancellationRequested)
		return false;

	static constexpr auto toBytePtr = [](const void* ptr) -> const std::byte* {
		return reinterpret_cast<const std::byte*>(ptr);
	};

	auto* mappedFile = toBytePtr(file.mmap(thin_io::file::mmap_access_mode::ReadOnly, 0, fileSize));
	if (!mappedFile) [[unlikely]]
	{
		assert_debug_only(mappedFile);
		return false;
	}

	for (uint64_t offset = 0; offset < fileSize; )
	{
		if (cancellationRequested)
			return false;

		static constexpr uint64_t maxLineLength = 4 * 1024;

		const auto maxSearchLength = std::min(fileSize - offset, maxLineLength);
		const auto lineStart = mappedFile + offset;
		offset += maxSearchLength;

		if (!useRawMemoryPattern) // Match using regex - slow(er)
		{
			alignas(4096) std::byte buffer[maxLineLength];
			static_assert(sizeof(buffer) % 16 == 0);

			::memcpy(buffer, lineStart, maxSearchLength);
			// Remove nulls from the contents so that QString ingests all data
			replace_null(buffer, maxSearchLength);

			const QString line = QString::fromUtf8((const char*)buffer, maxSearchLength);
			assert(!line.isEmpty());
			if (regex.match(line).hasMatch())
				return true;
		}
		else // Match the string directly - fast
		{
			if (memfind(lineStart, maxSearchLength, memoryPattern.constData(), memoryPattern.size()) != nullptr)
				return true;
		}
	}

	return false;
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

void CFileSearchEngine::notifySearchFinished(FileSearchListener* listener, SearchStatus status, uint64_t itemsScanned, uint64_t msElapsed)
{
	_searchInProgress = false;
	listener->searchFinished(status, itemsScanned, msElapsed);
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
				notifySearchFinished(listener, SearchInvalidPattern, 0, 0);
				return;
			}
		}
		else
			fileContentsPlainText = contentsToFind.toUtf8();
	}

	ContentSearchContext contentSearchContext{ listener, &fileContentsRegExp, &fileContentsPlainText, &cancellationRequested };
	std::unique_ptr<std::counting_semaphore<>> availableContentTaskSlots;
	std::unique_ptr<CThreadPool> contentSearchPool;

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn),
			[&](const CFileSystemObject& item, bool reachedThroughLink) {
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

				bool nameMatches = noFileNameFilter;
				if (!noFileNameFilter)
				{
					for (const auto& regexp : filterExpressions)
					{
						if (regexp.match(item.fullName()).hasMatch())
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
						availableContentTaskSlots = std::make_unique<std::counting_semaphore<>>(contentWorkerCount * 2);
						contentSearchContext.availableTaskSlots = availableContentTaskSlots.get();
						contentSearchPool = std::make_unique<CThreadPool>(contentWorkerCount, "File search by contents thread pool");
					}

					if (!acquireContentTaskSlotUnlessCancelled(*contentSearchContext.availableTaskSlots, cancellationRequested))
						return;

					contentSearchPool->enqueue([path{item.fullAbsolutePath()}, reachedThroughLink, &contentSearchContext] {
						EXEC_ON_SCOPE_EXIT([&contentSearchContext] { contentSearchContext.availableTaskSlots->release(); });
						if (*contentSearchContext.cancellationRequested)
							return;

						if (fileContentsMatches(path, *contentSearchContext.regex, *contentSearchContext.plainText, *contentSearchContext.cancellationRequested) &&
							!*contentSearchContext.cancellationRequested)
							contentSearchContext.listener->matchFound(path, reachedThroughLink);
					});
				}
				else
					listener->matchFound(item.fullAbsolutePath(), reachedThroughLink);

			}, cancellationRequested);
	}

	if (contentSearchPool)
	{
		// The queue is bounded and every task observes cancellation, so the same drain path is prompt on both normal and canceled exits.
		contentSearchPool->finishAllThreads(true);
	}

	const auto elapsedMs = timer.elapsed();
	notifySearchFinished(listener, cancellationRequested ? SearchCancelled : SearchFinished, itemCounter, elapsedMs);
}

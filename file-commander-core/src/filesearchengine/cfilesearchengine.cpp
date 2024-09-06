#include "cfilesearchengine.h"
#include "cfilesystemobject.h"
#include "system/ctimeelapsed.h"
#include "directoryscanner.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "file.hpp"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "hash/jenkins_hash.hpp"
#include "threading/thread_helpers.h"
#include "threading/cworkerthread.h"
#include "utility_functions/memory_functions.h"

DISABLE_COMPILER_WARNINGS
#include <QRegularExpression>
RESTORE_COMPILER_WARNINGS

[[nodiscard]] static QString queryToRegex(const QString& query, bool startToEnd)
{
	// Escape the dots
	QString regExString = QString{ query }.replace('.', QLatin1StringView{ "\\." });

	const bool nameQueryHasWildcards = query.contains('*') || query.contains('?');
	if (nameQueryHasWildcards)
	{
		regExString.replace('?', '.').replace('*', ".*");
		//regExString.prepend("\\A").append("\\z");
	}

	if (startToEnd)
	{
		if (!regExString.startsWith('^'))
			regExString.prepend('^');

		if (!regExString.endsWith('$'))
			regExString.append('$');
	}

	return regExString;
}

#ifndef __ARM_ARCH_ISA_A64

#include <smmintrin.h>  // SSE4.1

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

inline void replace_null(std::byte* array, size_t size)
{
	uint8x16_t old_neon = vdupq_n_u8(0);  // Duplicate old_value across all 16 bytes in the vector
	uint8x16_t new_neon = vdupq_n_u8(' ');  // Duplicate new_value across all 16 bytes in the vector

	for (size_t i = 0; i < size; i += 16)
	{
		uint8x16_t data = vld1q_u8(&array[i]);           // Load 16 bytes
		uint8x16_t mask = vceqq_u8(data, old_neon);      // Compare with old_value
		uint8x16_t result = vbslq_u8(mask, new_neon, data);  // Select new_value where mask is true, else original value
		vst1q_u8(&array[i], result);                     // Store the result back
	}
}
#endif

[[nodiscard]] static bool fileContentsMatches(const QString& path, const QRegularExpression& regex, const QByteArray& memoryPattern)
{
	thin_io::file file;
	if (!file.open(path.toUtf8().constData(), thin_io::file::open_mode::Read)) [[unlikely]]
		return false;

	const uint64_t fileSize = file.size().value_or(0);
	if (fileSize == 0) [[unlikely]]
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
		static constexpr uint64_t maxLineLength = 4 * 1024;

		const auto maxSearchLength = std::min(fileSize - offset, maxLineLength);
		const auto lineStart = mappedFile + offset;
		offset += maxSearchLength;

		if (memoryPattern.isEmpty()) // Match using regex - slow(er)
		{
			alignas(4096) std::byte buffer[maxLineLength];
			::memcpy(buffer, lineStart, maxSearchLength);
			// Remove nulls from the contents so that QString ingests all data
			replace_null(buffer, (maxSearchLength + 15) / 16);

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

bool CFileSearchEngine::searchInProgress() const
{
	return _workerThread.running();
}

bool CFileSearchEngine::search(
	const QString& what, bool subjectCaseSensitive,
	const QStringList& where,
	const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
	FileSearchListener* listener)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return true;
	}

	if (what.isEmpty() || where.empty())
		return false;

	_workerThread.exec([=, this] {
		searchThread(what, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive, contentsWholeWords, contentsIsRegex, listener);
	});

	return true;
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

void CFileSearchEngine::searchThread(
	const QString& what, bool subjectCaseSensitive,
	const QStringList& where,
	const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
	FileSearchListener* listener) noexcept
{
	::setThreadName("File search engine thread");

	uint64_t itemCounter = 0;
	CTimeElapsed timer;
	timer.start();

	const bool noFileNameFilter = (what == '*' || what.isEmpty());

	QRegularExpression queryRegExp;
	if (!noFileNameFilter)
	{
		queryRegExp.setPattern(queryToRegex(what, true));
		assert_r(queryRegExp.isValid());

		if (!subjectCaseSensitive)
			queryRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
	}

	const bool searchByContents = !contentsToFind.isEmpty();

	QRegularExpression fileContentsRegExp;
	QByteArray fileContentsPlainText;
	if (searchByContents)
	{
		if (contentsIsRegex)
		{
			QString pattern = queryToRegex(contentsToFind, false);
			if (contentsWholeWords)
				pattern.prepend("\\b").append("\\b");

			fileContentsRegExp.setPattern(pattern);

			if (!contentsCaseSensitive)
				fileContentsRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

			if (!fileContentsRegExp.isValid())
			{
				listener->searchFinished(SearchCancelled, 0, 0);
				return;
			}
		}
		else
			fileContentsPlainText = contentsToFind.toUtf8();
	}

	CWorkerThreadPool pool{ std::max(std::thread::hardware_concurrency(), 16u), "File search by contents thread pool"};

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn),
			[&](const CFileSystemObject& item) {

				if (itemCounter % 128 == 0)
				{
					// No need to report every single item and waste CPU cycles
					listener->itemScanned(item.fullAbsolutePath());
				}

				++itemCounter;

				if (searchByContents && !item.isFile())
					return;

				const bool nameMatches = noFileNameFilter || queryRegExp.match(item.fullName()).hasMatch();

				if (!nameMatches)
					return;

				if (searchByContents)
				{
					pool.enqueue([path{item.fullAbsolutePath()}, listener, &fileContentsRegExp, &fileContentsPlainText] {
						if (fileContentsMatches(path, fileContentsRegExp, fileContentsPlainText))
							listener->matchFound(path);
					});
				}
				else if (nameMatches)
				{
					listener->matchFound(item.fullAbsolutePath());
				}

			}, _workerThread.terminationFlag());
	}

	const bool interrupt = _workerThread.terminationFlag();
	pool.finishAllThreads(!interrupt); // On normal exit have to waut for all pending search tasks to complete; on abort terminate ASAP

	const auto elapsedMs = timer.elapsed();
	listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, itemCounter, elapsedMs);
}

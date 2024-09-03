#include "../ccontroller.h"
#include "system/ctimeelapsed.h"
#include "directoryscanner.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "file.hpp"

#include "hash/jenkins_hash.hpp"
#include "threading/thread_helpers.h"
#include "utility_functions/memory_functions.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QRegularExpression>
#include <QTextStream>
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

inline void replace_byte(std::byte* array, size_t size) noexcept
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

inline void replace_byte(std::byte* array, size_t size)
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

[[nodiscard]] static bool fileContentsMatches(const QString& path, const QRegularExpression& regex)
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


	static constexpr uint64_t maxLineLength = 8 * 1024;
	std::byte buffer[maxLineLength];

	for (uint64_t offset = 0; offset < fileSize; )
	{
		const auto maxSearchLength = std::min(fileSize - offset, maxLineLength);
		const auto lineStart = mappedFile + offset;
		offset += maxSearchLength;

		::memcpy(buffer, lineStart, maxSearchLength);
		replace_byte(buffer, (maxSearchLength + 15) / 16);

		QString line = QString::fromUtf8((const char*)buffer, maxSearchLength);
		assert(!line.isEmpty());
		if (regex.match(line).hasMatch())
			return true;
	}

	return false;
}

CFileSearchEngine::CFileSearchEngine(CController& controller) :
	_controller(controller),
	_workerThread("File search thread")
{
}

void CFileSearchEngine::addListener(CFileSearchEngine::FileSearchListener* listener)
{
	assert_and_return_r(listener, );
	_listeners.insert(listener);
}

void CFileSearchEngine::removeListener(CFileSearchEngine::FileSearchListener* listener)
{
	_listeners.erase(listener);
}

bool CFileSearchEngine::searchInProgress() const
{
	return _workerThread.running();
}

bool CFileSearchEngine::search(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords)
{
	if (_workerThread.running())
	{
		_workerThread.interrupt();
		return true;
	}

	if (what.isEmpty() || where.empty())
		return false;

	_workerThread.exec([=, this] {
		searchThread(what, subjectCaseSensitive, where, contentsToFind, contentsCaseSensitive, contentsWholeWords);
	});

	return true;
}

void CFileSearchEngine::stopSearching()
{
	_workerThread.interrupt();
}

void CFileSearchEngine::searchThread(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords) noexcept
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
	if (searchByContents)
	{
		QString pattern = queryToRegex(contentsToFind, false);
		if (contentsWholeWords)
			pattern.prepend("\\b").append("\\b");

		fileContentsRegExp.setPattern(pattern);

		if (!contentsCaseSensitive)
			fileContentsRegExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);

		assert_r(fileContentsRegExp.isValid());
	}

	const int uniqueJobTag = static_cast<int>(jenkins_hash("CFileSearchEngine")) + rand();

	QString line;

	const QByteArray contentsUtf8 = contentsToFind.toUtf8();

	for (const QString& pathToLookIn : where)
	{
		scanDirectory(CFileSystemObject(pathToLookIn),
			[&](const CFileSystemObject& item) {

				if (itemCounter % 8192 == 0)
				{
					// No need to report every single item and waste CPU cycles
					_controller.execOnUiThread([this, path{item.fullAbsolutePath()}]() {
						for (const auto& listener : _listeners)
							listener->itemScanned(path);
						}, uniqueJobTag);
				}

				++itemCounter;

				if (searchByContents && !item.isFile())
					return;

				const bool nameMatches = noFileNameFilter || queryRegExp.match(item.fullName()).hasMatch();

				if (!nameMatches)
					return;

				bool matchFound = false;

				if (searchByContents)
					matchFound = fileContentsMatches(item.fullAbsolutePath(), fileContentsRegExp);
				else
					matchFound = nameMatches;


				if (matchFound)
				{
					_controller.execOnUiThread([this, path{ item.fullAbsolutePath() }]() {
						for (const auto& listener : _listeners)
							listener->matchFound(path);
					});
				}

			}, _workerThread.terminationFlag());
	}

	const auto elapsedMs = timer.elapsed();
	_controller.execOnUiThread([this, elapsedMs, itemCounter]() {
		for (const auto& listener : _listeners)
			listener->searchFinished(_workerThread.terminationFlag() ? SearchCancelled : SearchFinished, itemCounter, elapsedMs);
	});
}

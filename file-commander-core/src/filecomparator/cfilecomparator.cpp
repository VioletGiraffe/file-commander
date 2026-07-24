#include "cfilecomparator.h"

#include "assert/advanced_assert.h"
#include "threading/thread_helpers.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <utility> // std::move

CFileComparator::~CFileComparator() noexcept
{
	abortComparison();
}

void CFileComparator::compareFilesThreaded(QString pathA, QString pathB, const std::function<void (int)>& progressCallback,
	const std::function<void (ContentComparisonResult)>& resultCallback)
{
	assert_debug_only(progressCallback);
	assert_debug_only(resultCallback);

	if (_comparisonThread.joinable())
		_comparisonThread.join();

	_terminate = false;
	_comparisonThread = std::thread([pathA{ std::move(pathA) }, pathB{ std::move(pathB) }, progressCallback, resultCallback, this]() {
		setThreadName("CFileComparator thread");

		const auto totalBytes = static_cast<uint64_t>(QFileInfo{ pathA }.size());
		uint64_t comparedBytes = 0;
		const auto result = compareFileContents(pathA, pathB, _terminate, [&](uint64_t bytesCompared) {
			comparedBytes += bytesCompared;
			progressCallback(static_cast<int>(comparedBytes * 100 / totalBytes)); // totalBytes cannot be 0 here: no bytes are reported for an empty file
		});

		progressCallback(100);
		resultCallback(result);
	});
}

void CFileComparator::abortComparison()
{
	if (_comparisonThread.joinable())
	{
		_terminate = true;
		_comparisonThread.join();
	}
}

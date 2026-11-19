#include "cfilecomparator.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <utility> // std::move

CFileComparator::CFileComparator(QString pathA, QString pathB)
{
	_task.start([this, pathA{ std::move(pathA) }, pathB{ std::move(pathB) }](const std::atomic<bool>& cancelled) -> std::optional<ContentComparisonResult> {
		const auto totalBytes = static_cast<uint64_t>(QFileInfo{ pathA }.size());
		uint64_t comparedBytes = 0;
		const auto result = compareFileContents(pathA, pathB, cancelled, [&](uint64_t bytesCompared) {
			comparedBytes += bytesCompared;
			_percent = static_cast<int>(comparedBytes * 100 / totalBytes); // totalBytes cannot be 0 here: no bytes are reported for an empty file
		});

		if (result == ContentComparisonResult::Aborted)
			return std::nullopt;
		return result;
	});
}

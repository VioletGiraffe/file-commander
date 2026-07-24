#include "filecontentcomparison.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <string.h>

ContentComparisonResult compareFileContents(const QString& pathA, const QString& pathB, const std::atomic<bool>& abort,
	const std::function<void (uint64_t bytesCompared)>& onProgress)
{
	QFile fileA{ pathA }, fileB{ pathB };
	if (!fileA.open(QFile::ReadOnly) || !fileB.open(QFile::ReadOnly))
		return ContentComparisonResult::Error;

	const qint64 size = fileA.size();
	if (size != fileB.size())
		return ContentComparisonResult::Different;

	static constexpr qint64 blockSize = 1024 * 1024;

	for (qint64 pos = 0; pos < size; pos += blockSize)
	{
		if (abort)
			return ContentComparisonResult::Aborted;

		const qint64 blockLength = std::min(blockSize, size - pos);

		uchar* const dataA = fileA.map(pos, blockLength);
		uchar* const dataB = fileB.map(pos, blockLength);
		const bool mapped = dataA != nullptr && dataB != nullptr;
		const bool blockEqual = mapped && ::memcmp(dataA, dataB, static_cast<size_t>(blockLength)) == 0;

		if (dataA)
			fileA.unmap(dataA);
		if (dataB)
			fileB.unmap(dataB);

		if (!mapped) [[unlikely]]
			return ContentComparisonResult::Error;

		if (!blockEqual)
			return ContentComparisonResult::Different;

		if (onProgress)
			onProgress(static_cast<uint64_t>(blockLength));
	}

	return ContentComparisonResult::Equal;
}

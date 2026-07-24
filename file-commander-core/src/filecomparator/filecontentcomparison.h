#pragma once

#include <atomic>
#include <functional>
#include <stdint.h>

class QString;

enum class ContentComparisonResult {
	Equal,
	Different,
	Aborted,
	Error // Either file could not be read; the contents remain unknown, which is not the same as differing
};

// Byte-wise comparison of two files. Progress is reported as the size of each block once it has been compared, rather
// than as a percentage, because only the caller knows the denominator: the file size for a single comparison, the
// total across all pairs for a batch of them.
ContentComparisonResult compareFileContents(const QString& pathA, const QString& pathB,
	const std::atomic<bool>& abort = std::atomic<bool>{false}, const std::function<void (uint64_t bytesCompared)>& onProgress = {});

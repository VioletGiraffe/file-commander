#pragma once
#include "cfilesystemobject.h"

#include <atomic>
#include <deque>
#include <stdint.h>
#include <vector>

struct FileStatistics
{
	inline bool operator==(const FileStatistics& other) const noexcept = default;
	[[nodiscard]] inline bool empty() const { return *this == FileStatistics{}; }

	std::deque<CFileSystemObject> largestFiles;

	uint64_t files = 0;
	uint64_t folders = 0;
	uint64_t occupiedSpace = 0;
};

[[nodiscard]] FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort = false);

#pragma once

#include <QString>

#include <atomic>
#include <stdint.h>
#include <vector>

struct FileStatistics
{
	inline constexpr bool operator==(const FileStatistics& other) const noexcept = default;
	[[nodiscard]] inline constexpr bool empty() const { return *this == FileStatistics{}; }

	uint64_t files = 0;
	uint64_t folders = 0;
	uint64_t occupiedSpace = 0;
};

[[nodiscard]] FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort = false);

#pragma once

#include <stdint.h>

struct FileStatistics
{
	inline constexpr bool operator==(const FileStatistics& other) const noexcept = default;
	[[nodiscard]] inline constexpr bool empty() const { return *this == FileStatistics{}; }

	uint64_t files = 0;
	uint64_t folders = 0;
	uint64_t occupiedSpace = 0;
};

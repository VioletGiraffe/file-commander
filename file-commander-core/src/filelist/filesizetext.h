#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <cstdint>

enum class FileSizeUnitSpacing {
	Spaced, // "1.5 K"
	Joined  // "1.5K"
};

// In the largest binary unit the size reaches, rounded down, with one decimal below 10: "1023", "1.0K" from 1024 bytes, "12K", "1023K"
[[nodiscard]] QString compactFileSizeText(uint64_t size, FileSizeUnitSpacing spacing);

#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <time.h>

enum class FileTimeTextLength {
	Short, // This year: "07 Mar 14:05"; other years: "21 Nov '24", the year in the time's place
	Long   // "21 Nov '24 09:40:12"
};

inline constexpr int maxShortFileTimeTextLength = 12; // This year's form

// A file's time in local time, with English month names in any locale
[[nodiscard]] QString fileTimeText(time_t time, FileTimeTextLength length);

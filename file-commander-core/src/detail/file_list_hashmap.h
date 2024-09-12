#pragma once

#include "ankerl/unordered_dense.h"

#include "cfilesystemobject.h"

struct NullHash {
	using is_avalanching = void;

	[[nodiscard]] inline constexpr qulonglong operator()(qulonglong hashValue) const noexcept {
		return hashValue;
	}
};

using FileListHashMap = ankerl::unordered_dense::segmented_map<qulonglong, CFileSystemObject, NullHash>;

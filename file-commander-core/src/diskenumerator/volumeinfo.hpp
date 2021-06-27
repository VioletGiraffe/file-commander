#pragma once

#include "cfilesystemobject.h"

#include <stdint.h>

struct VolumeInfo
{
	CFileSystemObject rootObjectInfo;
	QString volumeLabel;
	QString fileSystemName;
	uint64_t volumeSize = 0;
	uint64_t freeSize = 0;
	bool isReady = false;

	enum ComparisonResult {Equal, InsignificantChange, SignificantChange, DifferentObject};
	[[nodiscard]] ComparisonResult compare(const VolumeInfo& other) const noexcept {
		if (rootObjectInfo.hash() != other.rootObjectInfo.hash())
			return DifferentObject;
		else if (isReady != other.isReady)
			return SignificantChange;
		else if (freeSize != other.freeSize || volumeSize != other.volumeSize || volumeLabel != other.volumeLabel || fileSystemName != other.fileSystemName)
			return InsignificantChange;
		else
			return Equal;
	}

	[[nodiscard]] inline bool isEmpty() const noexcept {
		return rootObjectInfo.hash() == 0;
	}

	[[nodiscard]] inline uint64_t id() const noexcept {
		return rootObjectInfo.hash();
	}
};

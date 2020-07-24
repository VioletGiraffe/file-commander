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

	inline bool operator==(const VolumeInfo& other) const {
		return
			rootObjectInfo == other.rootObjectInfo &&
			volumeLabel == other.volumeLabel &&
			fileSystemName == other.fileSystemName &&
			volumeSize == other.volumeSize &&
			freeSize == other.freeSize &&
			isReady == other.isReady;
	}

	inline bool operator!=(const VolumeInfo& other) const {
		return !operator==(other);
	}

	inline bool isEmpty() const {
		return *this == VolumeInfo();
	}
};

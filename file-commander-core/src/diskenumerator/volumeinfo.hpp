#pragma once

#include "cfilesystemobject.h"

#include <stdint.h>

struct VolumeInfo
{
	CFileSystemObject fileSystemObject;
	QString volumeLabel;
	QString fileSystemName;
	uint64_t volumeSize = 0;
	uint64_t freeSize = 0;
	bool isReady = false;

	inline bool operator==(const VolumeInfo& other) const {
		return
				fileSystemObject == other.fileSystemObject &&
				volumeLabel == other.volumeLabel &&
				fileSystemName == other.fileSystemName &&
				volumeSize == other.volumeSize &&
				freeSize == other.volumeSize &&
				isReady == other.isReady;
	}
};

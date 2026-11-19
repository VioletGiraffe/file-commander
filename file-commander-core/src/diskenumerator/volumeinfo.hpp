#pragma once

#include "cfilesystemobject.h"


#include <stdint.h>

struct VolumeInfo
{
	enum class Kind {
		Internal, // also anything not told apart
		External, // USB, FireWire, a memory card or other removable media
		Optical,
		Network,
		DiskImage, // a mounted image file: VHD, ISO, DMG, a loop device
		RamDisk,
		Home // Linux's shortcut to the home folder, which need not be a volume
	};

	CFileSystemObject rootObjectInfo;
	QString volumeLabel;
	QString fileSystemName;
	QString devicePath; // Linux only: the block device, links resolved, e.g. /dev/sdb1. Empty for a network volume and the home folder.
	uint64_t volumeSize = 0;
	uint64_t freeSize = 0;
	Kind kind = Kind::Internal;
	bool isReady = false;

	enum ComparisonResult {Equal, InsignificantChange, SignificantChange, DifferentObject};
	[[nodiscard]] ComparisonResult compare(const VolumeInfo& other) const noexcept {
		if (rootObjectInfo.hash() != other.rootObjectInfo.hash())
			return DifferentObject;
		else if (isReady != other.isReady || volumeLabel != other.volumeLabel || kind != other.kind || devicePath != other.devicePath) // Outside Windows the label is the volume's displayed name
			return SignificantChange;
		else if (freeSize != other.freeSize || volumeSize != other.volumeSize || fileSystemName != other.fileSystemName)
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

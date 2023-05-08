#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	VolumeInfo info;
	info.rootObjectInfo = "/";
	info.volumeLabel = "root";
	info.isReady = true;

	const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath());
	info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
	info.freeSize = sys_info.f_bsize * sys_info.f_bavail;

	return std::vector<VolumeInfo>{std::move(info)};
}

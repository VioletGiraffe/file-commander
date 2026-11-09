#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl(std::vector<QString>& failureMessages)
{
	VolumeInfo info;
	info.rootObjectInfo = "/";
	info.volumeLabel = "root";
	info.isReady = true;

	const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath(), failureMessages);
	if (!sys_info)
		return {};

	info.volumeSize = sys_info->f_bsize * sys_info->f_blocks;
	info.freeSize = sys_info->f_bsize * sys_info->f_bavail;

	return std::vector<VolumeInfo>{std::move(info)};
}

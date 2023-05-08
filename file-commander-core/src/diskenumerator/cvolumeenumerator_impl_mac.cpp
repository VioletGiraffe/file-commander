#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::vector<VolumeInfo> volumes;

	for (const QString& volumeName: QDir("/Volumes/").entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System))
	{
		VolumeInfo info;
		info.volumeLabel = volumeName;
		info.isReady = true;

		const auto sys_info = volumeInfoForPath("/Volumes/" + volumeName);
		info.rootObjectInfo = sys_info.f_mntonname;
		info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
		info.freeSize = sys_info.f_bsize * sys_info.f_bavail;
		info.fileSystemName = QString(sys_info.f_fstypename).toUpper();

		volumes.emplace_back(std::move(info));
	}

	return volumes;
}

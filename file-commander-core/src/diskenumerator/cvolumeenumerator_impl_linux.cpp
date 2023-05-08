#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::vector<VolumeInfo> volumes;

	static const QString username = [] {
		QString name = qEnvironmentVariable("USER");
		if (name.isEmpty())
			name = qEnvironmentVariable("USERNAME");

		return name;
	}();
	static const QString mediaDirPath = QStringLiteral("/media/%1/").arg(username);

	const auto volumePaths = QDir{mediaDirPath}.entryList(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Readable);

	volumes.reserve(volumePaths.size() + 2);

	// Adding the two "default" volumes - home and root

	{
		VolumeInfo& homeInfo = volumes.emplace_back();
		homeInfo.rootObjectInfo = QStringLiteral("/home/%1/").arg(username);
		homeInfo.volumeLabel = "home";

		VolumeInfo& rootInfo = volumes.emplace_back();
		rootInfo.rootObjectInfo = "/";
		rootInfo.volumeLabel = "root";
	}

	for (auto&& extraVolumePath: volumePaths)
	{
		auto& info = volumes.emplace_back();
		info.rootObjectInfo = mediaDirPath + extraVolumePath;
		info.volumeLabel = info.rootObjectInfo.fullName();
	}

	for (auto& info: volumes)
	{
		const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath());
		info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
		info.freeSize = sys_info.f_bsize * sys_info.f_bavail;
		// TODO: pathIsAccessible()?
		info.isReady = true;
	}

	return volumes;
}

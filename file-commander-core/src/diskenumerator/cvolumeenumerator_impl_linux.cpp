#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <mntent.h>

#include <set>

namespace {

// Real, user-relevant mounts are always backed by either a block device (/dev/...) or a recognized network filesystem.
// Pseudo-filesystems (proc, sysfs, tmpfs, overlay for containers/snaps, squashfs for snap packages, etc.) have neither,
// which is what lets us tell them apart without having to chase an ever-changing list of virtual fs type names.
bool isPseudoFilesystem(const QString& fsType)
{
	static const std::set<QString> pseudoFsTypes = {
		QStringLiteral("sysfs"), QStringLiteral("proc"), QStringLiteral("devtmpfs"), QStringLiteral("devpts"),
		QStringLiteral("tmpfs"), QStringLiteral("securityfs"), QStringLiteral("cgroup"), QStringLiteral("cgroup2"),
		QStringLiteral("pstore"), QStringLiteral("efivarfs"), QStringLiteral("bpf"), QStringLiteral("autofs"),
		QStringLiteral("mqueue"), QStringLiteral("debugfs"), QStringLiteral("tracefs"), QStringLiteral("configfs"),
		QStringLiteral("fusectl"), QStringLiteral("hugetlbfs"), QStringLiteral("binfmt_misc"), QStringLiteral("ramfs"),
		QStringLiteral("rpc_pipefs"), QStringLiteral("nsfs"), QStringLiteral("overlay"), QStringLiteral("squashfs")
	};
	return pseudoFsTypes.contains(fsType);
}

bool isKnownNetworkFilesystem(const QString& fsType)
{
	static const std::set<QString> networkFsTypes = {
		QStringLiteral("nfs"), QStringLiteral("nfs4"), QStringLiteral("cifs"), QStringLiteral("smbfs"),
		QStringLiteral("smb3"), QStringLiteral("davfs"), QStringLiteral("afpfs")
	};
	return networkFsTypes.contains(fsType) || fsType.startsWith(QStringLiteral("fuse."));
}

}

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::vector<VolumeInfo> volumes;

	static const QString username = [] {
		QString name = qEnvironmentVariable("USER");
		if (name.isEmpty())
			name = qEnvironmentVariable("USERNAME");

		return name;
	}();
	static const QString homePath = QStringLiteral("/home/%1").arg(username);

	// "Home" is always shown as a convenience shortcut, even on setups where it isn't a mount point of its own.
	{
		VolumeInfo& homeInfo = volumes.emplace_back();
		homeInfo.rootObjectInfo = homePath + '/';
		homeInfo.volumeLabel = "home";
	}

	bool rootFound = false;

	// /proc/mounts always reflects the live mount table (unlike /etc/mtab, which can go stale), so real mount points
	// configured via fstab, udisks auto-mount (/media, /run/media), manual mounts under /mnt etc. are all picked up.
	if (FILE* mountsFile = setmntent("/proc/mounts", "r"))
	{
		while (mntent* entry = getmntent(mountsFile))
		{
			const QString fsType = QString::fromLocal8Bit(entry->mnt_type);
			if (isPseudoFilesystem(fsType))
				continue;

			const QString device = QString::fromLocal8Bit(entry->mnt_fsname);
			if (!device.startsWith(QStringLiteral("/dev/")) && !isKnownNetworkFilesystem(fsType))
				continue;

			const QString mountPoint = QString::fromLocal8Bit(entry->mnt_dir);
			if (mountPoint == homePath || mountPoint == QStringLiteral("/home"))
				continue; // Already covered by the synthetic "home" entry above

			if (mountPoint == QStringLiteral("/boot") || mountPoint.startsWith(QStringLiteral("/boot/")))
				continue; // Boot/ESP partitions are real filesystems, but not ones a user browses as a "drive"

			const bool isRoot = mountPoint == QStringLiteral("/");
			rootFound = rootFound || isRoot;

			auto& info = volumes.emplace_back();
			info.rootObjectInfo = isRoot ? mountPoint : mountPoint + '/';
			info.volumeLabel = isRoot ? QStringLiteral("root") : info.rootObjectInfo.fullName();
			info.fileSystemName = fsType.toUpper();
		}

		endmntent(mountsFile);
	}

	if (!rootFound)
	{
		VolumeInfo& rootInfo = volumes.emplace_back();
		rootInfo.rootObjectInfo = "/";
		rootInfo.volumeLabel = "root";
	}

	for (auto& info: volumes)
	{
		const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath());
		info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
		info.freeSize = sys_info.f_bsize * sys_info.f_bavail;
		// TODO: pathIsAccessible()?
		info.isReady = true;
	}

	// Remove all 0-size volumes, this should remove junk like vmblock-fuse
	std::erase_if(volumes, [](const VolumeInfo& v) { return v.volumeSize == 0; });

	return volumes;
}

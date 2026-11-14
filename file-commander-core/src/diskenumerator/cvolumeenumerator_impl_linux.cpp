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
		QStringLiteral("rpc_pipefs"), QStringLiteral("nsfs"), QStringLiteral("overlay"), QStringLiteral("squashfs"),
		QStringLiteral("fuse.portal") // The Flatpak document portal, /run/user/<uid>/doc; isKnownNetworkFilesystem() accepts any fuse.* type
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

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl(std::vector<QString>& failureMessages)
{
	std::vector<VolumeInfo> volumes;

	const QString homePath = QDir::homePath();

	// "Home" is always shown as a convenience shortcut, even on setups where it isn't a mount point of its own.
	{
		VolumeInfo& homeInfo = volumes.emplace_back();
		homeInfo.rootObjectInfo = homePath.endsWith('/') ? homePath : homePath + '/'; // homePath() is "/" where HOME is unset
		homeInfo.volumeLabel = "home";
	}

	bool rootFound = false;
	// The types of the filesystems holding the home folder and the root, from any mount, pseudo or not
	QString homeFsType, rootFsType;
	qsizetype homeMountPointLength = -1;

	// /proc/mounts always reflects the live mount table (unlike /etc/mtab, which can go stale), so real mount points
	// configured via fstab, udisks auto-mount (/media, /run/media), manual mounts under /mnt etc. are all picked up.
	if (FILE* mountsFile = setmntent("/proc/mounts", "r"))
	{
		while (mntent* entry = getmntent(mountsFile))
		{
			const QString fsType = QString::fromLocal8Bit(entry->mnt_type);
			const QString mountPoint = QString::fromLocal8Bit(entry->mnt_dir);

			// The deepest mount point at or above the home folder holds it
			// >=: a later mount on the same point hides the earlier one
			const bool holdsHome = mountPoint == QStringLiteral("/") || homePath == mountPoint || homePath.startsWith(mountPoint + '/');
			if (holdsHome && mountPoint.size() >= homeMountPointLength)
			{
				homeFsType = fsType;
				homeMountPointLength = mountPoint.size();
			}
			if (mountPoint == QStringLiteral("/"))
				rootFsType = fsType;

			if (isPseudoFilesystem(fsType))
				continue;

			const QString device = QString::fromLocal8Bit(entry->mnt_fsname);
			if (!device.startsWith(QStringLiteral("/dev/")) && !isKnownNetworkFilesystem(fsType))
				continue;

			if (mountPoint == homePath || (mountPoint == QStringLiteral("/home") && homePath.startsWith(QStringLiteral("/home/"))))
				continue; // Already covered by the synthetic "home" entry above

			if (mountPoint == QStringLiteral("/boot") || mountPoint.startsWith(QStringLiteral("/boot/")))
				continue; // Boot/ESP partitions are real filesystems, but not ones a user browses as a "drive"

			const bool isRoot = mountPoint == QStringLiteral("/");
			rootFound = rootFound || isRoot;

			auto& info = volumes.emplace_back();
			info.rootObjectInfo = isRoot ? mountPoint : mountPoint + '/';
			info.volumeLabel = isRoot ? QStringLiteral("root") : info.rootObjectInfo.fullName().toString();
			info.fileSystemName = fsType.toUpper();
		}

		endmntent(mountsFile);
	}

	volumes.front().fileSystemName = homeFsType.toUpper();

	if (!rootFound)
	{
		VolumeInfo& rootInfo = volumes.emplace_back();
		rootInfo.rootObjectInfo = "/";
		rootInfo.volumeLabel = "root";
		rootInfo.fileSystemName = rootFsType.toUpper();
	}

	std::vector<VolumeInfo> queriedVolumes;
	queriedVolumes.reserve(volumes.size());
	for (VolumeInfo& info: volumes)
	{
		const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath(), failureMessages);
		if (!sys_info)
			continue;

		info.volumeSize = sys_info->f_bsize * sys_info->f_blocks;
		info.freeSize = sys_info->f_bsize * sys_info->f_bavail;
		// TODO: canListDirectory()?
		info.isReady = true;

		if (info.volumeSize != 0) // A 0-size volume is junk like vmblock-fuse
			queriedVolumes.push_back(std::move(info));
	}

	return queriedVolumes;
}

#include "cvolumeenumerator.h"
#include "volumeinfohelper.hpp"


// Submodule includes
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

#include <string.h>

// session: null when DASessionCreate() failed
static VolumeInfo::Kind volumeKind(const struct statfs& volume, DASessionRef session)
{
	if ((volume.f_flags & MNT_LOCAL) == 0)
		return VolumeInfo::Kind::Network;

	CFDictionaryRef description = nullptr;
	if (session)
	{
		// DiskArbitration takes the BSD name without the /dev/ prefix
		const char* bsdName = volume.f_mntfromname;
		if (strncmp(bsdName, "/dev/", 5) == 0)
			bsdName += 5;

		if (DADiskRef disk = ::DADiskCreateFromBSDName(kCFAllocatorDefault, session, bsdName))
		{
			description = ::DADiskCopyDescription(disk);
			::CFRelease(disk);
		}
	}
	EXEC_ON_SCOPE_EXIT([description] { if (description) ::CFRelease(description); });

	// Absent keys read as null
	const auto descriptionValue = [description](CFStringRef key) { return description ? ::CFDictionaryGetValue(description, key) : nullptr; };

	const auto model = static_cast<CFStringRef>(descriptionValue(kDADiskDescriptionDeviceModelKey));
	if (model && QString::fromCFString(model).trimmed() == QStringLiteral("Disk Image"))
		return VolumeInfo::Kind::DiskImage;

	// Not by file system: a USB stick may hold an ISO 9660 image, and a hard disk may be formatted as UDF
	const auto mediaKind = static_cast<CFStringRef>(descriptionValue(kDADiskDescriptionMediaKindKey));
	if (mediaKind)
	{
		const QString mediaClass = QString::fromCFString(mediaKind);
		if (mediaClass == QStringLiteral("IOCDMedia") || mediaClass == QStringLiteral("IODVDMedia") || mediaClass == QStringLiteral("IOBDMedia"))
			return VolumeInfo::Kind::Optical;
	}

	const auto internal = static_cast<CFBooleanRef>(descriptionValue(kDADiskDescriptionDeviceInternalKey));
	const auto removable = static_cast<CFBooleanRef>(descriptionValue(kDADiskDescriptionMediaRemovableKey));
	if ((internal && !::CFBooleanGetValue(internal)) || (removable && ::CFBooleanGetValue(removable)))
		return VolumeInfo::Kind::External;

	return VolumeInfo::Kind::Internal;
}

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl(std::vector<QString>& failureMessages)
{
	std::vector<VolumeInfo> volumes;

	DASessionRef session = ::DASessionCreate(kCFAllocatorDefault);
	EXEC_ON_SCOPE_EXIT([session] { if (session) ::CFRelease(session); });

	for (const QString& volumeName: QDir("/Volumes/").entryList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Hidden | QDir::System))
	{
		const auto sys_info = volumeInfoForPath("/Volumes/" + volumeName, failureMessages);
		if (!sys_info)
			continue;

		VolumeInfo info;
		info.volumeLabel = volumeName;
		info.isReady = true;
		info.rootObjectInfo = sys_info->f_mntonname;
		info.volumeSize = sys_info->f_bsize * sys_info->f_blocks;
		info.freeSize = sys_info->f_bsize * sys_info->f_bavail;
		info.fileSystemName = QString(sys_info->f_fstypename).toUpper();
		info.kind = volumeKind(*sys_info, session);

		volumes.emplace_back(std::move(info));
	}

	return volumes;
}

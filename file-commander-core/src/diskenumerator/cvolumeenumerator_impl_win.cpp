#include "cvolumeenumerator.h"


// Submodule includes
#include "directoryscan/thinioqstring.h"
#include "system/win_utils.hpp"
#include "timing/ctimeelapsed.h"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <Windows.h>
#include <winioctl.h>

#include <cstddef>
#include <optional>
#include <vector>

// rootPath: "X:\". Empty when the query fails, e.g. for a volume that spans several disks.
static std::optional<STORAGE_BUS_TYPE> storageBusType(const QString& rootPath)
{
	const QString volumePath = QStringLiteral("\\\\.\\") + rootPath.left(2);
	// Zero access: the query needs no rights, so no elevation
	const HANDLE volume = ::CreateFileW(reinterpret_cast<const WCHAR*>(volumePath.utf16()), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (volume == INVALID_HANDLE_VALUE)
		return std::nullopt;
	EXEC_ON_SCOPE_EXIT([volume] { ::CloseHandle(volume); });

	STORAGE_PROPERTY_QUERY query{};
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;

	// The header holds the size of the whole descriptor
	STORAGE_DESCRIPTOR_HEADER header{};
	DWORD bytesReturned = 0;
	if (::DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &bytesReturned, nullptr) == 0
		|| header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR))
		return std::nullopt;

	std::vector<std::byte> descriptorBuffer(header.Size);
	if (::DeviceIoControl(volume, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), descriptorBuffer.data(), header.Size, &bytesReturned, nullptr) == 0)
		return std::nullopt;

	return reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(descriptorBuffer.data())->BusType;
}

// rootPath: "X:\"
static VolumeInfo::Kind volumeKind(const QString& rootPath)
{
	const UINT driveType = ::GetDriveTypeW(reinterpret_cast<const WCHAR*>(rootPath.utf16()));
	if (driveType == DRIVE_REMOTE)
		return VolumeInfo::Kind::Network;
	if (driveType == DRIVE_RAMDISK)
		return VolumeInfo::Kind::RamDisk;
	if (driveType == DRIVE_REMOVABLE)
		return VolumeInfo::Kind::External;

	// The drive type does not tell these apart: a USB hard disk reports as fixed, a mounted ISO image as a CD-ROM drive
	const std::optional<STORAGE_BUS_TYPE> busType = storageBusType(rootPath);
	const bool virtualBus = busType == BusTypeVirtual || busType == BusTypeFileBackedVirtual;
	if (driveType == DRIVE_CDROM)
		return virtualBus ? VolumeInfo::Kind::DiskImage : VolumeInfo::Kind::Optical;
	if (virtualBus)
		return VolumeInfo::Kind::DiskImage;
	if (busType == BusTypeUsb || busType == BusType1394 || busType == BusTypeSd || busType == BusTypeMmc)
		return VolumeInfo::Kind::External;

	return VolumeInfo::Kind::Internal;
}

static VolumeInfo volumeInfoForDriveLetter(const QString& driveLetter, std::vector<QString>& failureMessages)
{
	if (!canListDirectory(driveLetter))
		return {};

	WCHAR volumeName[256], filesystemName[256];
	const DWORD error = GetVolumeInformationW(reinterpret_cast<const WCHAR*>(driveLetter.utf16()), volumeName, 256, nullptr, nullptr, nullptr, filesystemName, 256) != 0 ? 0 : GetLastError();

	if (error != 0 && error != ERROR_NOT_READY)
	{
		failureMessages.push_back(QStringLiteral("GetVolumeInformationW() failed for ") + driveLetter + QStringLiteral(": ") + QString::fromStdString(ErrorStringFromLastError()));
		return {};
	}

	VolumeInfo info;
	info.isReady = error != ERROR_NOT_READY;

	info.rootObjectInfo = driveLetter;
	info.kind = volumeKind(driveLetter);

	if (info.isReady)
	{
		ULARGE_INTEGER totalSpace, freeSpace;
		if (GetDiskFreeSpaceExW(reinterpret_cast<const WCHAR*>(driveLetter.utf16()), &freeSpace, &totalSpace, nullptr) != 0)
		{
			info.volumeSize = totalSpace.QuadPart;
			info.freeSize = freeSpace.QuadPart;
		}
		else
			failureMessages.push_back(QStringLiteral("GetDiskFreeSpaceExW() failed for ") + driveLetter + QStringLiteral(": ") + QString::fromStdString(ErrorStringFromLastError()));
	}

	info.volumeLabel = QString::fromWCharArray(volumeName);
	info.fileSystemName = QString::fromWCharArray(filesystemName);
	return info;
}

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl(std::vector<QString>& failureMessages)
{
	std::vector<VolumeInfo> volumes;

	const auto oldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	EXEC_ON_SCOPE_EXIT([oldErrorMode]() {::SetErrorMode(oldErrorMode);});

	DWORD drives = ::GetLogicalDrives();
	if (drives == 0)
	{
		failureMessages.push_back(QStringLiteral("GetLogicalDrives() failed: ") + QString::fromStdString(ErrorStringFromLastError()));
		return volumes;
	}

	for (char driveLetter = 'A'; drives != 0 && driveLetter <= 'Z'; ++driveLetter, drives >>= 1)
	{
		if ((drives & 0x1) == 0)
			continue;

		CTimeElapsed timer{ true };
		auto volumeInfo = volumeInfoForDriveLetter(QString(driveLetter) + QStringLiteral(":\\"), failureMessages);
		const auto elapsedMs = timer.elapsed();
		if (elapsedMs > 100)
			qInfo() << "volumeInfoForDriveLetter for" << QString(driveLetter) + QStringLiteral(":\\") << "took" << elapsedMs << "ms";

		if (!volumeInfo.isEmpty())
			volumes.emplace_back(std::move(volumeInfo));
	}

	return volumes;
}

#include "cvolumeenumerator.h"
#include "windows/windowsutils.h"
#include "utility/on_scope_exit.hpp"

#include <Windows.h>

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

inline QString parseVolumePathFromPathsList(WCHAR* paths)
{
	QString qstring;
	for (auto string = paths; string[0] != L'\0'; string += wcslen(string) + 1)
	{
		qstring = QString::fromWCharArray(string);
		if (qstring.contains(':'))
			return qstring;
	}

	return qstring;
}

inline VolumeInfo volumeInfoForDriveLetter(const QString& driveLetter)
{
	VolumeInfo info;

	WCHAR volumeName[256], filesystemName[256];
	const DWORD error = GetVolumeInformationW((WCHAR*)driveLetter.utf16(), volumeName, 256, nullptr, nullptr, nullptr, filesystemName, 256) != 0 ? 0 : GetLastError();

	if (error != 0 && error != ERROR_NOT_READY)
	{
		qInfo() << "GetVolumeInformationW() returned error:" << ErrorStringFromLastError();
		return info;
	}
	else
		info.isReady = error != ERROR_NOT_READY;

	info.rootObjectInfo = driveLetter;

	if (info.isReady)
	{
		ULARGE_INTEGER totalSpace, freeSpace;
		if (GetDiskFreeSpaceExW((WCHAR*)driveLetter.utf16(), &freeSpace, &totalSpace, nullptr) != 0)
		{
			info.volumeSize = totalSpace.QuadPart;
			info.freeSize = freeSpace.QuadPart;
		}
		else
			qInfo() << "GetDiskFreeSpaceExW() returned error:" << ErrorStringFromLastError();
	}

	info.volumeLabel = QString::fromWCharArray(volumeName);
	info.fileSystemName = QString::fromWCharArray(filesystemName);
	return info;
}

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::deque<VolumeInfo> volumes;

	const auto oldErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	EXEC_ON_SCOPE_EXIT([oldErrorMode]() {SetErrorMode(oldErrorMode);});

	DWORD drives = GetLogicalDrives();
	if (drives == 0)
	{
		qInfo() << "GetLogicalDrives() returned an error:" << ErrorStringFromLastError();
		return volumes;
	}

	for (char driveLetter = 'A'; drives != 0 && driveLetter <= 'Z'; ++driveLetter, drives >>= 1)
	{
		if ((drives & 0x1) == 0)
			continue;

		const auto volumeInfo = volumeInfoForDriveLetter(QString(driveLetter) + QStringLiteral(":\\"));
		if (!volumeInfo.isEmpty())
			volumes.push_back(volumeInfo);
	}

	return volumes;
}

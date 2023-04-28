#include "cvolumeenumerator.h"
#include "../filesystemhelpers/filesystemhelpers.hpp"
#include "system/win_utils.hpp"
#include "utility/on_scope_exit.hpp"

#include "system/ctimeelapsed.h"

#include <Windows.h>

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

inline QString parseVolumePathFromPathsList(WCHAR* paths)
{
	QString qstring;
	for (auto* string = paths; string[0] != L'\0'; string += wcslen(string) + 1)
	{
		qstring = QString::fromWCharArray(string);
		if (qstring.contains(':'))
			return qstring;
	}

	return qstring;
}

static VolumeInfo volumeInfoForDriveLetter(const QString& driveLetter)
{
	if (!FileSystemHelpers::pathIsAccessible(driveLetter))
		return {};

	WCHAR volumeName[256], filesystemName[256];
	const DWORD error = GetVolumeInformationW(reinterpret_cast<const WCHAR*>(driveLetter.utf16()), volumeName, 256, nullptr, nullptr, nullptr, filesystemName, 256) != 0 ? 0 : GetLastError();

	if (error != 0 && error != ERROR_NOT_READY)
	{
		const auto text = QString::fromStdString(ErrorStringFromLastError());
		qInfo() << "GetVolumeInformationW() returned error for" << driveLetter << text;
		return {};
	}

	VolumeInfo info;
	info.isReady = error != ERROR_NOT_READY;

	info.rootObjectInfo = driveLetter;

	if (info.isReady)
	{
		ULARGE_INTEGER totalSpace, freeSpace;
		if (GetDiskFreeSpaceExW(reinterpret_cast<const WCHAR*>(driveLetter.utf16()), &freeSpace, &totalSpace, nullptr) != 0)
		{
			info.volumeSize = totalSpace.QuadPart;
			info.freeSize = freeSpace.QuadPart;
		}
		else
			qInfo() << "GetDiskFreeSpaceExW() returned error:" << QString::fromStdString(ErrorStringFromLastError());
	}

	info.volumeLabel = QString::fromWCharArray(volumeName);
	info.fileSystemName = QString::fromWCharArray(filesystemName);
	return info;
}

std::vector<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::vector<VolumeInfo> volumes;

	const auto oldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	EXEC_ON_SCOPE_EXIT([oldErrorMode]() {::SetErrorMode(oldErrorMode);});

	DWORD drives = ::GetLogicalDrives();
	if (drives == 0)
	{
		qInfo() << "GetLogicalDrives() returned an error:" << QString::fromStdString(ErrorStringFromLastError());
		return volumes;
	}

	for (char driveLetter = 'A'; drives != 0 && driveLetter <= 'Z'; ++driveLetter, drives >>= 1)
	{
		if ((drives & 0x1) == 0)
			continue;

		CTimeElapsed timer{ true };
		auto volumeInfo = volumeInfoForDriveLetter(QString(driveLetter) + QStringLiteral(":\\"));
		const auto elapsedMs = timer.elapsed();
		if (elapsedMs > 100)
			qInfo() << "volumeInfoForDriveLetter for" << QString(driveLetter) + QStringLiteral(":\\") << "took" << elapsedMs << "ms";

		if (!volumeInfo.isEmpty())
			volumes.emplace_back(std::move(volumeInfo));
	}

	return volumes;
}

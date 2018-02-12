#include "cvolumeenumerator.h"
#include "assert/advanced_assert.h"
#include "container/algorithms.hpp"
#include "container/set_operations.hpp"
#include "utility/on_scope_exit.hpp"
#include "volumeinfohelper.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

void CVolumeEnumerator::addObserver(IVolumeListObserver *observer)
{
	assert_r(std::find(_observers.begin(), _observers.end(), observer) == _observers.end());
	_observers.push_back(observer);
}

void CVolumeEnumerator::removeObserver(IVolumeListObserver *observer)
{
	ContainerAlgorithms::erase_all_occurrences(_observers, observer);
}

// Returns the drives found
std::deque<VolumeInfo> CVolumeEnumerator::drives() const
{
	std::lock_guard<typename decltype(_mutexForDrives)> lock(_mutexForDrives);

	return _drives;
}

void CVolumeEnumerator::updateSynchronously()
{
	enumerateVolumes(false);
}

CVolumeEnumerator::CVolumeEnumerator() : _enumeratorThread(_updateInterval, "CVolumeEnumerator thread")
{
	// Setting up the timer to fetch the notifications from the queue and execute them on this thread
	connect(&_timer, &QTimer::timeout, [this](){
		_notificationsQueue.exec();
	});
	_timer.start(_updateInterval / 3);

	// Starting the worker thread that actually enumerates the volumes
	_enumeratorThread.start([this](){
		enumerateVolumes(true);
	});
}

// Refresh the list of available volumes
void CVolumeEnumerator::enumerateVolumes(bool async)
{
	const auto newDrives = enumerateVolumesImpl();

	std::lock_guard<typename decltype(_mutexForDrives)> lock(_mutexForDrives);

	if (!async || newDrives != _drives)
	{
		_drives.resize(newDrives.size());
		std::copy(newDrives.cbegin(), newDrives.cend(), _drives.begin());

		notifyObservers(async);
	}
}

// Calls all the registered observers with the latest list of drives found
void CVolumeEnumerator::notifyObservers(bool async) const
{
	// This method is called from the worker thread
	// Queuing the code to be executed on the thread where CVolumeEnumerator was created

	_notificationsQueue.enqueue([this]() {
		for (auto& observer : _observers)
			observer->volumesChanged();
	}, 0); // Setting the tag to 0 will discard any previous queue items with the same tag that have not yet been processed

	if (!async)
		_notificationsQueue.exec();
}


///////////////////////////////////////////////
//      SYSTEM-SPECIFIC IMPLEMENTATION
///////////////////////////////////////////////

#if defined _WIN32

#include "windows/windowsutils.h"

#include <Windows.h>

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

#elif defined __APPLE__

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	VolumeInfo info;
	info.rootObjectInfo = "/";
	info.volumeLabel = "root";
	info.isReady = true;

	const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath());
	info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
	info.freeSize = sys_info.f_bsize * sys_info.f_bavail;

	return std::deque<VolumeInfo>(1, info);
}

#elif defined __linux__

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	VolumeInfo info;
	info.rootObjectInfo = "/";
	info.volumeLabel = "root";
	info.isReady = true;

	const auto sys_info = volumeInfoForPath(info.rootObjectInfo.fullAbsolutePath());
	info.volumeSize = sys_info.f_bsize * sys_info.f_blocks;
	info.freeSize = sys_info.f_bsize * sys_info.f_bavail;

	return std::deque<VolumeInfo>(1, info);
}

#else

#error(Unknown OS)

#endif

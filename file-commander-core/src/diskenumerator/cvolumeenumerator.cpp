#include "cvolumeenumerator.h"
#include "assert/advanced_assert.h"
#include "container/algorithms.hpp"
#include "container/set_operations.hpp"

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
const std::deque<VolumeInfo>& CVolumeEnumerator::drives() const
{
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
		qstring = QString::fromUtf16((char16_t*)string);
		if (qstring.contains(':'))
			return qstring;
	}

	return qstring;
}

inline VolumeInfo volumeInfoForGuid(WCHAR* volumeGuid)
{
	VolumeInfo info;

	WCHAR volumeName[256], filesystemName[256];
	const DWORD error = GetVolumeInformationW(volumeGuid, volumeName, 256, nullptr, nullptr, nullptr, filesystemName, 256) != 0 ? 0 : GetLastError();

	if (error != 0 && error != ERROR_NOT_READY)
	{
		qDebug() << "GetVolumeInformationW() returned error:" << ErrorStringFromLastError();
		return info;
	}
	else
		info.isReady = error != ERROR_NOT_READY;

	WCHAR pathNames[256];
	DWORD numNamesReturned = 0;
	if (GetVolumePathNamesForVolumeNameW(volumeGuid, pathNames, 256, &numNamesReturned) != 0)
		info.rootObjectInfo = CFileSystemObject(numNamesReturned == 1 ? QString::fromUtf16((char16_t*)pathNames) : parseVolumePathFromPathsList(pathNames));
	else
		qDebug() << "GetVolumePathNamesForVolumeNameW() returned error:" << ErrorStringFromLastError();

	if (info.isReady)
	{
		ULARGE_INTEGER totalSpace, freeSpace;
		if (GetDiskFreeSpaceExW(volumeGuid, &freeSpace, &totalSpace, nullptr) != 0)
		{
			info.volumeSize = totalSpace.QuadPart;
			info.freeSize = freeSpace.QuadPart;
		}
		else
			qDebug() << "GetDiskFreeSpaceExW() returned error:" << ErrorStringFromLastError();
	}

	info.volumeLabel = QString::fromUtf16((char16_t*)volumeName);
	info.fileSystemName = QString::fromUtf16((char16_t*)filesystemName);
	return info;
}

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::deque<VolumeInfo> volumes;

	WCHAR volumeId[64];
	HANDLE volumeSearchHandle = FindFirstVolumeW(volumeId, 64);

	if (volumeSearchHandle == INVALID_HANDLE_VALUE)
	{
		qDebug() << "Failed to find first volume. Error:" << ErrorStringFromLastError();
		return volumes;
	}
	else
	{
		const VolumeInfo info = volumeInfoForGuid(volumeId);
		if (info != VolumeInfo())
			volumes.push_back(info);
	}

	for(;;)
	{
		const BOOL findNextVolumeResult = FindNextVolumeW(volumeSearchHandle, volumeId, 64);
		if (findNextVolumeResult == 0)
			break;

		const VolumeInfo info = volumeInfoForGuid(volumeId);
		if (info != VolumeInfo())
			volumes.push_back(info);
	}

	if (GetLastError() != ERROR_NO_MORE_FILES)
		qDebug() << "FindNextVolume returned an error:" << ErrorStringFromLastError();

	FindVolumeClose(volumeSearchHandle);

	std::sort(begin_to_end(volumes), [](const VolumeInfo& l, const VolumeInfo& r) {return l.rootObjectInfo.fullAbsolutePath() < r.rootObjectInfo.fullAbsolutePath();});
	return volumes;
}

#elif defined __APPLE__

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::deque<VolumeInfo> volumes;
	return volumes;
}

#elif defined __linux__

const std::deque<VolumeInfo> CVolumeEnumerator::enumerateVolumesImpl()
{
	std::deque<VolumeInfo> volumes;
	return volumes;
}

#else

#error(Unknown OS)

#endif

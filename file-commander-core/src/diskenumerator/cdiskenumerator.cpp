#include "cdiskenumerator.h"
#include <QDir>
#include <QIcon>
#include <assert.h>

#if defined __linux__
#include <mntent.h>
#include <stdio.h>
#elif defined __APPLE__
#include <sys/mount.h>
#elif defined _WIN32
#include <Windows.h>
#endif

CDiskEnumerator &CDiskEnumerator::instance()
{
	static CDiskEnumerator instance;
	return instance;
}

void CDiskEnumerator::addObserver(IDiskListObserver *observer)
{
	assert(std::find(_observers.begin(), _observers.end(), observer) == _observers.end());
	_observers.push_back(observer);
	if (!_timer.isActive())
	{
		_timer.start();
		_timer.singleShot(0, this, SLOT(enumerateDisks()));
	}
}

void CDiskEnumerator::removeObserver(IDiskListObserver *observer)
{
	_observers.erase(std::remove(_observers.begin(), _observers.end(), observer), _observers.end());
	if (_observers.empty())
		_timer.stop();
}

const std::vector<CDiskEnumerator::Drive> &CDiskEnumerator::drives() const
{
	return _drives;
}

// Returns the drives found
CDiskEnumerator::CDiskEnumerator()
{
	_timer.setInterval(1000);
	connect(&_timer, SIGNAL(timeout()), SLOT(enumerateDisks()));
}

// Refresh the list of available disk drives
void CDiskEnumerator::enumerateDisks()
{
	std::vector<Drive> newDriveList;
	bool changesDetected = false;

#if defined _WIN32
	const auto drives = QDir::drives();
	for (const auto& drive: drives)
	{
		const QString drivePath(drive.absoluteFilePath());
		const QString driveLetter(QString(drivePath).remove("/").remove("\\").remove(":"));

		WCHAR volumeLabelBuffer[1000] = {0};
		const BOOL succ = GetVolumeInformationW(drivePath.toStdWString().data(), volumeLabelBuffer, 1000-1, NULL, NULL, NULL, NULL, 0);

		QString volumeLabel(succ !=0 ? QString::fromWCharArray(volumeLabelBuffer) : driveLetter);
		if (volumeLabel.isEmpty())
			volumeLabel = driveLetter;
		newDriveList.emplace_back(Drive(drive, driveLetter, volumeLabel));
	}
#elif defined __linux__
	FILE *f = setmntent(_PATH_MOUNTED, "r");
	assert(f);
	struct mntent * m = getmntent(f);
	for (; m != 0; m = getmntent(f))
	{
		const QString source = QString::fromLocal8Bit(m->mnt_fsname);
		const QString mountPoint = QString::fromLocal8Bit(m->mnt_dir);
		if (source.contains("/dev/sd"))
		{
			QString displayName;
			if (mountPoint.contains("/media/"))
				displayName = QString(mountPoint).remove("/media/");
			else
				displayName = QString(source).remove("/dev/");
			newDriveList.emplace_back(Drive(mountPoint, displayName, QString("Disk drive [%2] mounted at %1").arg(mountPoint).arg(QString::fromLocal8Bit(m->mnt_type))));
		}
	}

	endmntent(f);
#elif defined __APPLE__
	struct statfs* mounts = 0;
	const int numMounts = getmntinfo(&mounts, MNT_WAIT);
	for (int i = 0; i < numMounts; i++)
	{
		const QString mountPoint(mounts[i].f_mntonname);
		newDriveList.emplace_back(Drive(mountPoint, mountPoint, mountPoint));
	}
#else
	#error unknown platform
#endif

	for (const auto& newDrive: newDriveList)
	{
		if (!changesDetected && std::find(_drives.begin(), _drives.end(), newDrive) == _drives.end())
			// This disk has been added since last enumeration
			changesDetected = true;
	}

	changesDetected = changesDetected || newDriveList.size() != _drives.size();
	_drives = newDriveList;
	if (changesDetected)
		notifyObservers();
}

void CDiskEnumerator::notifyObservers() const
{
	for (auto& observer: _observers)
		observer->disksChanged();
}

#include "cdiskenumerator.h"
#include <assert.h>

#include "utils/utils.h"

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

const QList<QStorageInfo> &CDiskEnumerator::drives() const
{
	return _drives;
}

// Returns the drives found
CDiskEnumerator::CDiskEnumerator()
{
	_timer.setInterval(1000);
	connect(&_timer, SIGNAL(timeout()), SLOT(enumerateDisks()));
}

static const auto storageInfoLessComp = [](const QStorageInfo& l, const QStorageInfo& r)
{
	return (l.name() + l.rootPath() + QString::number(l.bytesAvailable())) < (r.name() + r.rootPath() + QString::number(r.bytesAvailable()));
};

// Refresh the list of available disk drives
void CDiskEnumerator::enumerateDisks()
{
	const auto newDrives = QStorageInfo::mountedVolumes();
	if (setTheoreticDifference<std::vector>(newDrives, _drives, storageInfoLessComp) != setTheoreticDifference<std::vector>(_drives, newDrives, storageInfoLessComp))
	{
		_drives = newDrives;
		notifyObservers();
	}
}

void CDiskEnumerator::notifyObservers() const
{
	for (auto& observer: _observers)
		observer->disksChanged();
}

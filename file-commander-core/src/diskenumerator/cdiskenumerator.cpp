#include "cdiskenumerator.h"
#include <assert.h>

#include "utils/utils.h"

void CDiskEnumerator::addObserver(IDiskListObserver *observer)
{
	assert(std::find(_observers.begin(), _observers.end(), observer) == _observers.end());
	_observers.push_back(observer);
}

void CDiskEnumerator::removeObserver(IDiskListObserver *observer)
{
	_observers.erase(std::remove(_observers.begin(), _observers.end(), observer), _observers.end());
}

const QList<QStorageInfo> &CDiskEnumerator::drives() const
{
	return _drives;
}

// Returns the drives found
CDiskEnumerator::CDiskEnumerator() : _enumeratorThread(_updateInterval)
{
	connect(&_timer, &QTimer::timeout, [this](){
		_notificationsQueue.exec();
	});
	_timer.start(_updateInterval / 3);

	_enumeratorThread.start([this](){
		enumerateDisks();
	});
}

inline bool drivesChanged(const QList<QStorageInfo>& l, const QList<QStorageInfo>& r)
{
	if (l.size() != r.size())
		return true;

	for (int i = 0; i < l.size(); ++i)
		if ((l[i].name() % l[i].rootPath() % QString::number(l[i].bytesAvailable())) != (r[i].name() % r[i].rootPath() % QString::number(r[i].bytesAvailable())))
			return true;

	return false;
}

// Refresh the list of available disk drives
void CDiskEnumerator::enumerateDisks()
{
	const auto newDrives = QStorageInfo::mountedVolumes();

	if (drivesChanged(newDrives, _drives))
	{
		_drives = newDrives;
		notifyObservers();
	}
}

void CDiskEnumerator::notifyObservers() const
{
	_notificationsQueue.enqueue([this]() {
		for (auto& observer : _observers)
			observer->disksChanged();
	}, 0);
}

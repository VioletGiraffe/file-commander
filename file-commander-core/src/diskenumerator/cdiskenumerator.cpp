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

inline bool operator<(const QStorageInfo& l, const QStorageInfo& r)
{
	return (l.name() + l.rootPath() + QString::number(l.bytesAvailable())) < (r.name() + r.rootPath() + QString::number(r.bytesAvailable()));
}

inline bool drivesChanged(const QList<QStorageInfo>& l, const QList<QStorageInfo>& r)
{
	if (l.size() != r.size())
		return true;

	for (int i = 0; i < l.size(); ++i)
		if (l[i] < r[i] || r[i] < l[i])
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
	for (auto& observer: _observers)
		observer->disksChanged();
}

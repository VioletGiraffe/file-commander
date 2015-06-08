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

// Returns the drives found
const QList<QStorageInfo> &CDiskEnumerator::drives() const
{
	return _drives;
}

CDiskEnumerator::CDiskEnumerator() : _enumeratorThread(_updateInterval, "CDiskEnumerator thread")
{
	// Setting up the timer to fetch the notifications from the queue and execute them on this thread
	connect(&_timer, &QTimer::timeout, [this](){
		_notificationsQueue.exec();
	});
	_timer.start(_updateInterval / 3);

	// Starting the worker thread that actually enumerates the disks
	_enumeratorThread.start([this](){
		enumerateDisks();
	});
}

// A helper function that checks if there are any changes between the old and the new disk lists - including the change in space available
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

// Calls all the registered observers with the latest list of drives found
void CDiskEnumerator::notifyObservers() const
{
	// This method is called from the worker thread
	// Queuing the code to be executed on the thread where CDiskEnumerator was created

	_notificationsQueue.enqueue([this]() {
		for (auto& observer : _observers)
			observer->disksChanged();
	}, 0); // Setting the tag to 0 will discard any previous queue items with the same tag that have not yet been processed
}

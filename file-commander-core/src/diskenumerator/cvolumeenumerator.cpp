#include "cvolumeenumerator.h"
#include "assert/advanced_assert.h"
#include "container/algorithms.hpp"
#include "container/set_operations.hpp"

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
std::vector<VolumeInfo> CVolumeEnumerator::drives() const
{
	std::lock_guard lock{ _mutexForDrives };

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
	auto newDrives = enumerateVolumesImpl();

	std::lock_guard lock{ _mutexForDrives };

	VolumeInfo::ComparisonResult levelOfChange = newDrives.size() != _drives.size() ? VolumeInfo::DifferentObject : VolumeInfo::Equal;
	if (levelOfChange == VolumeInfo::Equal)
	{
		for (size_t i = 0, n = _drives.size(); i < n; ++i)
		{
			const auto comparisonResult = newDrives[i].compare(_drives[i]);
			levelOfChange = std::max(levelOfChange, comparisonResult);
		}
	}


	if (!async || levelOfChange != VolumeInfo::Equal)
	{
		_drives = std::move(newDrives);
		notifyObservers(async, levelOfChange > VolumeInfo::InsignificantChange);
	}
}

// Calls all the registered observers with the latest list of drives found
void CVolumeEnumerator::notifyObservers(bool async, bool drivesListOrReadinessChanged) const
{
	// This method is called from the worker thread
	// Queuing the code to be executed on the thread where CVolumeEnumerator was created

	_notificationsQueue.enqueue([this, drivesListOrReadinessChanged]() {
		for (auto& observer : _observers)
			observer->volumesChanged(drivesListOrReadinessChanged);
	}, 0); // Setting the tag to 0 will discard any previous queue items with the same tag that have not yet been processed

	if (!async)
		_notificationsQueue.exec();
}

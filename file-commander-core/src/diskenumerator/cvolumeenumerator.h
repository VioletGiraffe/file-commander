#pragma once

#include "volumeinfo.hpp"
#include "threading/cexecutionqueue.h"
#include "threading/cperiodicexecutionthread.h"

DISABLE_COMPILER_WARNINGS
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <deque>

// Lists all the volumes available on a target machine
class CVolumeEnumerator : protected QObject
{
public:
	CVolumeEnumerator();

	// Volumes list observer interface
	class IVolumeListObserver
	{
	public:
		virtual ~IVolumeListObserver() = default;
		virtual void volumesChanged() = 0;
	};

	// Adds the observer
	void addObserver(IVolumeListObserver * observer);
	// Removes the observer
	void removeObserver(IVolumeListObserver * observer);
	// Returns the drives found
	const std::deque<VolumeInfo>& drives() const;

	// Forces an update in this thread
	void updateSynchronously();

private:
	// Refresh the list of available volumes
	void enumerateVolumes(bool async);

	// Calls all the registered observers with the latest list of drives found
	void notifyObservers(bool async) const;

	static const std::deque<VolumeInfo> enumerateVolumesImpl();

private:
	std::deque<VolumeInfo> _drives;
	std::deque<IVolumeListObserver*> _observers;
	mutable CExecutionQueue          _notificationsQueue;
	CPeriodicExecutionThread         _enumeratorThread;
	QTimer                           _timer;

	const unsigned int _updateInterval = 1000; // ms
};

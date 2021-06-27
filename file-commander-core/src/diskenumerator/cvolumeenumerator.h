#pragma once

#include "volumeinfo.hpp"
#include "threading/cexecutionqueue.h"
#include "threading/cperiodicexecutionthread.h"

DISABLE_COMPILER_WARNINGS
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <mutex>
#include <optional>
#include <vector>

// Lists all the volumes available on a target machine
class CVolumeEnumerator final : public QObject
{
public:
	CVolumeEnumerator();

	void startEnumeratorThread();

	// Volumes list observer interface
	class IVolumeListObserver
	{
	public:
		virtual ~IVolumeListObserver() = default;
		virtual void volumesChanged(bool drivesListOrReadinessChanged /* Basically, distinguishes between significant and insignificant changes */) noexcept = 0;
	};

	// Adds the observer
	void addObserver(IVolumeListObserver * observer);
	// Removes the observer
	void removeObserver(IVolumeListObserver * observer);
	// Returns the drives found
	[[nodiscard]] std::vector<VolumeInfo> volumes() const;
	[[nodiscard]] std::optional<VolumeInfo> volumeById(uint64_t id) const noexcept;

	// Forces an update in this thread
	void updateSynchronously();

private:
	// Refresh the list of available volumes
	void enumerateVolumes(bool async);

	// Calls all the registered observers with the latest list of drives found
	void notifyObservers(bool async, bool drivesListOrReadinessChanged /* Basically, distinguishes between significant and insignificant changes */) const;

	[[nodiscard]] static const std::vector<VolumeInfo> enumerateVolumesImpl();

private:
	std::vector<VolumeInfo> _volumes;
	mutable std::recursive_mutex _mutexForDrives; // Has to be recursive:
	// enumerateVolumes() can be called synchronously through updateSynchronously(), and then drives() getter will fail to acquire the mutex unless it's recursive

	std::vector<IVolumeListObserver*> _observers;
	mutable CExecutionQueue          _notificationsQueue;
	CPeriodicExecutionThread         _enumeratorThread;
	QTimer                           _timer;

	static constexpr unsigned int _updateInterval = 1000; // ms
};

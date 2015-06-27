#ifndef CDISKENUMERATOR_H
#define CDISKENUMERATOR_H

#include "cfilesystemobject.h"
#include "utils/threading/cexecutionqueue.h"
#include "utils/threading/cperiodicexecutionthread.h"

#include <QStorageInfo>
#include <QTimer>

#include <vector>


// Lists all the disk drives available on a target machine
class CDiskEnumerator : protected QObject
{
public:
	struct DiskInfo
	{
		DiskInfo() = default;
		DiskInfo(const QStorageInfo& qStorageInfo) : storageInfo(qStorageInfo), fileSystemObject(qStorageInfo.rootPath()) {}

		QStorageInfo      storageInfo;
		CFileSystemObject fileSystemObject;
	};


	CDiskEnumerator();

	// Disk list observer interface
	class IDiskListObserver
	{
	public:
		virtual ~IDiskListObserver() {}
		virtual void disksChanged() = 0;
	};

	// Adds the observer
	void addObserver(IDiskListObserver * observer);
	// Removes the observer
	void removeObserver(IDiskListObserver * observer);
	// Returns the drives found
	const std::vector<DiskInfo>& drives() const;

	// Forces an update in this thread
	void updateSynchronously();

private:
	// Refresh the list of available disk drives
	void enumerateDisks(bool async);

	// Calls all the registered observers with the latest list of drives found
	void notifyObservers(bool async) const;

private:
	std::vector<DiskInfo>           _drives;
	std::vector<IDiskListObserver*> _observers;
	mutable CExecutionQueue         _notificationsQueue;
	CPeriodicExecutionThread        _enumeratorThread;
	QTimer                          _timer;

	static const unsigned int       _updateInterval = 1000; // ms
};

#endif // CDISKENUMERATOR_H

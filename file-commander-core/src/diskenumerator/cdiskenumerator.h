#ifndef CDISKENUMERATOR_H
#define CDISKENUMERATOR_H

#include "../cfilesystemobject.h"
#include "../utils/threading/cexecutionqueue.h"
#include "../utils/threading/cperiodicexecutionthread.h"

#include "QtCoreIncludes"

#include <vector>

// Lists all the disk drives available on a target machine
class CDiskEnumerator : protected QObject
{
	Q_OBJECT

public:
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
	const QList<QStorageInfo>& drives() const;

private slots:
	// Refresh the list of available disk drives
	void enumerateDisks();

private:
	// Calls all the registered observers with the latest list of drives found
	void notifyObservers() const;

private:
	QList<QStorageInfo>             _drives;
	std::vector<IDiskListObserver*> _observers;
	mutable CExecutionQueue         _notificationsQueue;
	CPeriodicExecutionThread        _enumeratorThread;
	QTimer                          _timer;

	static const int _updateInterval = 1000; // ms
};

#endif // CDISKENUMERATOR_H

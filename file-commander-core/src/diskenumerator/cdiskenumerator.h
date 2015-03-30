#ifndef CDISKENUMERATOR_H
#define CDISKENUMERATOR_H

#include "../cfilesystemobject.h"
#include "QtCoreIncludes"

#include <vector>

// Lists all the disk drives available on a target machine
class CDiskEnumerator : protected QObject
{
	Q_OBJECT

	CDiskEnumerator();

public:
	// Disk list observer interface
	class IDiskListObserver
	{
	public:
		virtual ~IDiskListObserver() {}
		virtual void disksChanged() = 0;
	};

	static CDiskEnumerator& instance();
	// Adds the observer
	void addObserver(IDiskListObserver * observer);
	// Removes the observer
	void removeObserver(IDiskListObserver * observer);
	// Returns the drives found
	const QList<QStorageInfo>& drives() const;

private slots:
	// Refresh the list of available disk drives
	void enumerateDisks ();

private:
	void notifyObservers() const;

private:
	QList<QStorageInfo>             _drives;
	std::vector<IDiskListObserver*> _observers;
	QTimer                          _timer;
};

#endif // CDISKENUMERATOR_H

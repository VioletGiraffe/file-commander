#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "filesystem_types.hpp" // thin_io
#include "threading/cperiodicexecutionthread.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <mutex>
#include <set>
#include <stdint.h>

// One child of the watched folder: ordered by name, equal when the size matches too
struct SnapshotEntry
{
	thin_io::native_string name;
	uint64_t size = 0; // A link's target's

	[[nodiscard]] bool operator<(const SnapshotEntry& other) const noexcept { return name < other.name; }
	[[nodiscard]] bool operator==(const SnapshotEntry& other) const noexcept { return name == other.name && size == other.size; }
};

class CFileSystemWatcherTimerBased
{
public:
	CFileSystemWatcherTimerBased();
	~CFileSystemWatcherTimerBased();

	CFileSystemWatcherTimerBased(const CFileSystemWatcherTimerBased&) = delete;
	CFileSystemWatcherTimerBased& operator=(const CFileSystemWatcherTimerBased&) = delete;

	// This method is thread-safe.
	bool setPathToWatch(const QString &path);
	// The poll loop takes its baseline up to a period after setPathToWatch, so a change made in that gap is
	// absorbed into the baseline and never reported. Call this when you take your own snapshot of the folder, to
	// pin the baseline to that moment instead. Scans synchronously on the calling thread.
	void captureBaselineState();
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void onCheckForChanges();
	[[nodiscard]] static std::set<SnapshotEntry> snapshotDirectory(const QString& path);
	void processChangesAndNotifySubscribers(std::set<SnapshotEntry>&& newState, uint64_t pathGeneration);

private:
	CPeriodicExecutionThread _periodicThread{ 400 /* period in ms*/, "CFileSystemWatcher thread" };
	// Written by the poll thread and by captureBaselineState() (panel worker thread); all access under _mutex.
	std::set<SnapshotEntry> _previousState;
	uint64_t _previousStateGeneration = 0;

	std::recursive_mutex _mutex;
	QString _pathToWatch;
	uint64_t _pathGeneration = 0;

	std::atomic_bool _bChangeDetected = false;
};

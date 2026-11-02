#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "directory_snapshot.hpp" // thin_io
#include "threading/cperiodicexecutionthread.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <mutex>
#include <optional>
#include <stdint.h>

// Detects changes in one folder by comparing periodic snapshots, for folders where change notifications may be incomplete.
// The polling thread starts on the first path to watch.
class CDirectorySnapshotPoller
{
public:
	explicit CDirectorySnapshotPoller(unsigned int periodMs = 2000);
	~CDirectorySnapshotPoller();

	CDirectorySnapshotPoller(const CDirectorySnapshotPoller&) = delete;
	CDirectorySnapshotPoller& operator=(const CDirectorySnapshotPoller&) = delete;

	// An empty path stops polling. Only called from one thread at a time.
	void setPathToWatch(const QString &path);
	// The poll loop takes its baseline up to a period after setPathToWatch, so a change made in that gap is
	// absorbed into the baseline and never reported. Call this when you take your own snapshot of the folder, to
	// pin the baseline to that moment instead. Scans synchronously on the calling thread.
	void captureBaselineState();
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void onCheckForChanges();
	// nullopt when the folder can't be listed: deleting an empty folder must still differ from its baseline.
	[[nodiscard]] static std::optional<thin_io::directory_snapshot> snapshotDirectory(const QString& path);
	void processChangesAndNotifySubscribers(std::optional<thin_io::directory_snapshot>&& newState, uint64_t pathGeneration);

private:
	CPeriodicExecutionThread _periodicThread;
	bool _threadStarted = false; // Only setPathToWatch() touches it
	// Written by the poll thread and by captureBaselineState() (panel worker thread); all access under _mutex.
	std::optional<thin_io::directory_snapshot> _previousState;
	uint64_t _previousStateGeneration = 0;

	std::mutex _mutex;
	QString _pathToWatch;
	uint64_t _pathGeneration = 0;

	std::atomic_bool _bChangeDetected = false;
};

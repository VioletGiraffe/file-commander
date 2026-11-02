#include "cdirectorysnapshotpoller.h"


// Submodule includes
#include "qt_paths.hpp" // thin_io

#include <utility>

CDirectorySnapshotPoller::CDirectorySnapshotPoller(unsigned int periodMs) :
	_periodicThread{ periodMs, "CDirectorySnapshotPoller thread" }
{
}

CDirectorySnapshotPoller::~CDirectorySnapshotPoller()
{
	_periodicThread.terminate();
}

void CDirectorySnapshotPoller::setPathToWatch(const QString& path)
{
	{
		std::lock_guard locker{ _mutex };
		_pathToWatch = path;
		++_pathGeneration;
		_bChangeDetected = false;
	}

	if (path.isEmpty())
	{
		if (_threadStarted)
			_periodicThread.pause(); // Nothing to watch: park the thread instead of letting it poll for no reason.
	}
	else if (std::exchange(_threadStarted, true))
		_periodicThread.resume(); // No-op if it wasn't paused (e.g. just navigating within the already-active tab).
	else
	{
		_periodicThread.start([this] {
			onCheckForChanges();
		}, 100 /* delay before start*/);
	}
}

bool CDirectorySnapshotPoller::changesDetected() noexcept
{
	bool expected = true;
	return _bChangeDetected.compare_exchange_strong(expected, false);
}

void CDirectorySnapshotPoller::onCheckForChanges()
{
	QString pathToWatch;
	uint64_t pathGeneration;
	{
		std::lock_guard locker{ _mutex };
		if (_pathToWatch.isEmpty())
			return;

		pathToWatch = _pathToWatch;
		pathGeneration = _pathGeneration;
	}

	processChangesAndNotifySubscribers(snapshotDirectory(pathToWatch), pathGeneration);
}

// Baseline and poll scans must use the same listing_detail, else every poll diffs against a differently-built
// baseline and reports phantom changes. Sharing this call guarantees it.
std::optional<thin_io::directory_snapshot> CDirectorySnapshotPoller::snapshotDirectory(const QString& path)
{
	auto snapshot = thin_io::directory_snapshot::take(thin_io::native_path(path).data(), thin_io::listing_detail::full);
	if (!snapshot)
		return {};

	return std::move(*snapshot);
}

void CDirectorySnapshotPoller::processChangesAndNotifySubscribers(std::optional<thin_io::directory_snapshot>&& newState, uint64_t pathGeneration)
{
	std::lock_guard locker{ _mutex };
	if (_pathToWatch.isEmpty() || pathGeneration != _pathGeneration)
		return;

	// captureBaselineState() writes _previousState from another thread, so this comparison can't be hoisted out of the lock.
	if (_previousStateGeneration == pathGeneration && newState != _previousState)
		_bChangeDetected = true;

	_previousState.swap(newState);
	_previousStateGeneration = pathGeneration;
}

void CDirectorySnapshotPoller::captureBaselineState()
{
	uint64_t pathGeneration;
	QString pathToWatch;
	{
		std::lock_guard locker{ _mutex };
		// Nothing watched, or the current path is already baselined (here or by the poll thread); advancing it is
		// the poll loop's job.
		if (_pathToWatch.isEmpty() || _previousStateGeneration == _pathGeneration)
			return;

		pathToWatch = _pathToWatch;
		pathGeneration = _pathGeneration;
	}

	// Scanned without the lock: it can block on a slow volume, and holding the lock would stall the other watcher methods.
	auto snapshot = snapshotDirectory(pathToWatch);

	std::lock_guard locker{ _mutex };
	// Discard if a navigation superseded us mid-scan, or a baseline landed meanwhile - clobbering it could
	// re-hide an already-detected change.
	if (_pathGeneration != pathGeneration || _previousStateGeneration == pathGeneration)
		return;

	_previousState.swap(snapshot);
	_previousStateGeneration = pathGeneration;
}

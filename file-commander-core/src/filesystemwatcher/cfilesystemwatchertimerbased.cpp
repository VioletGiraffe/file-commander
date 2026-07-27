#include "cfilesystemwatchertimerbased.h"
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "container/set_operations.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

FileSystemInfoWrapper::FileSystemInfoWrapper(QFileInfo&& fullInfo) noexcept :
	_info{std::move(fullInfo)},
	_itemName(_info.fileName())
{}

bool FileSystemInfoWrapper::operator<(const FileSystemInfoWrapper& other) const noexcept
{
	return _itemName < other._itemName;
}

bool FileSystemInfoWrapper::operator==(const FileSystemInfoWrapper& other) const noexcept
{
	return _itemName == other._itemName && size() == other.size();
}

qint64 FileSystemInfoWrapper::size() const noexcept
{
	if (_size == -1)
		_size = _info.size();

	return _size;
}

CFileSystemWatcherTimerBased::CFileSystemWatcherTimerBased()
{
	_periodicThread.start([this] {
		onCheckForChanges();
	}, 100 /* delay before start*/);
}

CFileSystemWatcherTimerBased::~CFileSystemWatcherTimerBased()
{
	_periodicThread.terminate();
}

bool CFileSystemWatcherTimerBased::setPathToWatch(const QString& path)
{
	assert_and_return_r(path.isEmpty() || QFileInfo(path).isDir(), false);

	{
		std::lock_guard locker{ _mutex };
		_pathToWatch = path;
		++_pathGeneration;
		_bChangeDetected = false;
	}

	if (path.isEmpty())
		_periodicThread.pause(); // Nothing to watch: park the thread instead of letting it poll for no reason.
	else
		_periodicThread.resume(); // No-op if it wasn't paused (e.g. just navigating within the already-active tab).

	return true;
}

bool CFileSystemWatcherTimerBased::changesDetected() noexcept
{
	bool expected = true;
	return _bChangeDetected.compare_exchange_strong(expected, false);
}

void CFileSystemWatcherTimerBased::onCheckForChanges()
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

// Baseline and poll scans must use identical filters, else every poll diffs against a differently-built
// baseline and reports phantom changes. Sharing this call guarantees it.
std::set<FileSystemInfoWrapper> CFileSystemWatcherTimerBased::snapshotDirectory(const QString& path)
{
	std::set<FileSystemInfoWrapper> snapshot;
	for (auto&& info : QDir{ path }.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
		snapshot.emplace(std::move(info));

	return snapshot;
}

void CFileSystemWatcherTimerBased::processChangesAndNotifySubscribers(std::set<FileSystemInfoWrapper>&& newState, uint64_t pathGeneration)
{
	std::lock_guard locker{ _mutex };
	if (_pathToWatch.isEmpty() || pathGeneration != _pathGeneration)
		return;

	// captureBaselineState() writes _previousState from another thread, so this comparison can't be hoisted out of the lock.
	if (_previousStateGeneration == pathGeneration && !SetOperations::is_equal_sets(newState, _previousState))
		_bChangeDetected = true;

	_previousState.swap(newState);
	_previousStateGeneration = pathGeneration;
}

void CFileSystemWatcherTimerBased::captureBaselineState()
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

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

	QDir directory(pathToWatch);
	auto state = directory.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	processChangesAndNotifySubscribers(std::move(state), pathGeneration);
}

void CFileSystemWatcherTimerBased::processChangesAndNotifySubscribers(QFileInfoList&& newState, uint64_t pathGeneration)
{
	std::set<FileSystemInfoWrapper> newItemsSet;
	for (auto&& info : newState)
		newItemsSet.emplace(std::move(info));

	// _previousState is polling-thread-only, so the potentially expensive comparison needs no mutex. A path
	// change during the scan or comparison is rejected by the generation check below.
	const bool differenceFound = _previousStateGeneration == pathGeneration && !SetOperations::is_equal_sets(newItemsSet, _previousState);

	{
		std::lock_guard locker{ _mutex };
		if (_pathToWatch.isEmpty() || pathGeneration != _pathGeneration)
			return;

		if (differenceFound)
			_bChangeDetected = true;

		_previousState.swap(newItemsSet);
		_previousStateGeneration = pathGeneration;
	}
}

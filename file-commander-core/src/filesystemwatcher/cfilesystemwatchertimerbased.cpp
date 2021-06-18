#include "cfilesystemwatchertimerbased.h"
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "system/ctimeelapsed.h"
#include "container/set_operations.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

FileSystemInfoWrapper::FileSystemInfoWrapper(QFileInfo&& fullInfo) noexcept :
	_info{std::move(fullInfo)},
	_fullPath(_info.fileName())
{}

bool FileSystemInfoWrapper::operator<(const FileSystemInfoWrapper& other) const noexcept
{
	return _fullPath < other._fullPath;
}

bool FileSystemInfoWrapper::operator==(const FileSystemInfoWrapper& other) const noexcept
{
	return _fullPath == other._fullPath && size() == other.size() && modificationTime() == other.modificationTime();
}

qint64 FileSystemInfoWrapper::size() const noexcept
{
	if (_size == -1)
		_size = _info.size();

	return _size;
}

uint FileSystemInfoWrapper::modificationTime() const noexcept
{
	if (_modificationTime == 0)
		_modificationTime = _info.lastModified().toTime_t();

	return _modificationTime;
}

CFileSystemWatcherTimerBased::CFileSystemWatcherTimerBased()
{
	QObject::connect(&_timer, &QTimer::timeout, [this]() {onCheckForChanges();});
	_timer.start(333);
}

bool CFileSystemWatcherTimerBased::setPathToWatch(const QString& path)
{
	std::lock_guard<std::recursive_mutex> locker(_pathMutex);

	assert_and_return_r(path.isEmpty() || QFileInfo(path).isDir(), false);

	_pathToWatch = path;
	return true;
}

void CFileSystemWatcherTimerBased::onCheckForChanges()
{
	std::unique_lock locker{ _pathMutex };
	if (_pathToWatch.isEmpty())
		return;

	QDir directory(_pathToWatch);
	locker.unlock();

	auto state = directory.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);

	CTimeElapsed timer{ true };

	processChangesAndNotifySubscribers(std::move(state));

	const auto elapsed = timer.elapsed();
	if (elapsed > 1)
		qInfo() << __FUNCTION__ << elapsed;
}

void CFileSystemWatcherTimerBased::processChangesAndNotifySubscribers(QFileInfoList&& newState)
{
	std::set<FileSystemInfoWrapper> newItemsSet;
	for (auto&& info : std::move(newState))
		newItemsSet.emplace(std::move(info));

	const bool differenceFound = !SetOperations::is_equal_sets(newItemsSet, _previousState);
	if (newItemsSet.size() > 100)
		qInfo() << differenceFound;

	if (differenceFound)
		notifySubscribers();

	_previousState = std::move(newItemsSet);
}

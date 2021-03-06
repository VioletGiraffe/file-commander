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

	std::lock_guard locker(_mutex);
	_pathToWatch = path;
	return true;
}

void CFileSystemWatcherTimerBased::onCheckForChanges()
{
	std::unique_lock locker{ _mutex };
	if (_pathToWatch.isEmpty())
		return;

	QDir directory(_pathToWatch);
	locker.unlock();

	auto state = directory.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	processChangesAndNotifySubscribers(std::move(state));
}

void CFileSystemWatcherTimerBased::processChangesAndNotifySubscribers(QFileInfoList&& newState)
{
	std::set<FileSystemInfoWrapper> newItemsSet;
	for (auto&& info : std::move(newState))
		newItemsSet.emplace(std::move(info));

	const bool differenceFound = !SetOperations::is_equal_sets(newItemsSet, _previousState);
	if (differenceFound)
		notifySubscribers();

	_previousState = std::move(newItemsSet);
}

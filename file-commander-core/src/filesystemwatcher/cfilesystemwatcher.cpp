#include "cfilesystemwatcher.h"
#include "assert/advanced_assert.h"
#include "container/set_operations.hpp"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

void detail::CFileSystemWatcherInterface::addCallback(ChangeDetectedCallback callback)
{
	_callbacks.push_back(callback);
}

inline bool operator==(const QFileInfo& fullInfo, const BasicFileSystemItemInfo& basicInfo)
{
	return fullInfo.absoluteFilePath() == basicInfo.fullPath;
}

inline bool operator<(const QFileInfo& fullInfo, const BasicFileSystemItemInfo& basicInfo)
{
	return fullInfo.absoluteFilePath() < basicInfo.fullPath;
}

void detail::CFileSystemWatcherInterface::processChangesAndNotifySubscribers(const QFileInfoList& newState)
{
	// Note: QFileInfo::operator== does exactly what's needed
	// http://doc.qt.io/qt-5/qfileinfo.html#operator-eq-eq
	// If this changes, a custom comparator will be required

	std::set<QFileInfo, std::less<>> newItemsSet;
	std::copy(begin_to_end(newState), std::inserter(newItemsSet, newItemsSet.end()));

	const auto diff = SetOperations::calculateDiff(_previousState, newItemsSet);

	transparent_set<QFileInfo> changedItems;
	for (const auto& newItem : diff.common_elements)
	{
		const auto sameOldItem = container_aware_find(_previousState, newItem);
		assert_debug_only(sameOldItem != _previousState.end());
		if (sameOldItem->fileDetailsChanged(newItem))
			changedItems.insert(newItem);
	}

	if (!changedItems.empty() || !diff.elements_from_a_not_in_b.empty() || !diff.elements_from_b_not_in_a.empty())
	{
		for (const auto& callback : _callbacks)
			callback(diff.elements_from_b_not_in_a, diff.elements_from_a_not_in_b, changedItems);

		_previousState.clear();
		std::copy(begin_to_end(newState), std::inserter(_previousState, _previousState.end()));
	}
}



CFileSystemWatcher::CFileSystemWatcher()
{
	QObject::connect(&_timer, &QTimer::timeout, [this]() {onCheckForChanges();});
	_timer.start(333);
}

bool CFileSystemWatcher::setPathToWatch(const QString& path)
{
	std::lock_guard<std::recursive_mutex> locker(_pathMutex);

	assert_and_return_r(path.isEmpty() || QFileInfo(path).isDir(), false);

	_pathToWatch = path;
	return true;
}

void CFileSystemWatcher::onCheckForChanges()
{
	{
		std::lock_guard<std::recursive_mutex> locker(_pathMutex);
		if (_pathToWatch.isEmpty())
			return;
	}

	QDir directory(_pathToWatch);
	const auto state = directory.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	processChangesAndNotifySubscribers(state);
}

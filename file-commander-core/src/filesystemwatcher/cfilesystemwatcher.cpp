#include "cfilesystemwatcher.h"
#include "assert/advanced_assert.h"
#include "container/set_operations.hpp"

#include <QDateTime>
#include <QDir>

void detail::CFileSystemWatcherInterface::addCallback(ChangeDetectedCallback callback)
{
	_callbacks.push_back(callback);
}

inline bool fileInfoChanged(const QFileInfo& oldInfo, const QFileInfo& newInfo)
{
	return
		oldInfo.size() != newInfo.size() ||
		oldInfo.lastModified() != newInfo.lastModified() ||
		oldInfo.created() != newInfo.created();
}

void detail::CFileSystemWatcherInterface::processChangesAndNotifySubscribers(const QFileInfoList& newState)
{
	// Note: QFileInfo::operator== does exactly what's needed
	// http://doc.qt.io/qt-5/qfileinfo.html#operator-eq-eq
	// If this changes, a custom comparator will be required
	const auto diff = SetOperations::calculateDiff<QFileInfoList, QFileInfoList, std::deque<QFileInfo>>(_previousState, newState);

	std::deque<QFileInfo> changedItems;
	for (const auto& oldItem : diff.common_elements)
	{
		const auto sameNewItem = std::find(begin_to_end(newState), oldItem);
		assert(sameNewItem != newState.end());
		if (fileInfoChanged(oldItem, *sameNewItem))
			changedItems.push_back(*sameNewItem);
	}

	if (!changedItems.empty() || !diff.elements_from_a_not_in_b.empty() || !diff.elements_from_b_not_in_a.empty())
	{
		for (const auto& callback : _callbacks)
			callback(diff.elements_from_b_not_in_a, diff.elements_from_a_not_in_b, changedItems);

		_previousState = newState;
	}
}



CFileSystemWatcher::CFileSystemWatcher()
{
	_timer.setInterval(333);
	QObject::connect(&_timer, &QTimer::timeout, [this]() {onCheckForChanges();});
}

bool CFileSystemWatcher::setPathToWatch(const QString& path)
{
	_pathToWatch = path;
	assert_and_return_r(QFileInfo(path).isDir(), false);

	_timer.start();
	return true;
}

void CFileSystemWatcher::onCheckForChanges()
{
	const auto state = QDir(_pathToWatch).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	processChangesAndNotifySubscribers(state);
}

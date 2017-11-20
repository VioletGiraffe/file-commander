#include "cfilesystemwatcher.h"
#include "assert/advanced_assert.h"
#include "container/set_operations.hpp"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

void detail::CFileSystemWatcherInterface::addCallback(ChangeDetectedCallback callback)
{
	_callbacks.push_back(callback);
}

inline bool fileInfoChanged(const QFileInfo& oldInfo, const QFileInfo& newInfo)
{
	return oldInfo.size() != newInfo.size() || oldInfo.lastModified() != newInfo.lastModified();
}

void detail::CFileSystemWatcherInterface::processChangesAndNotifySubscribers(const QFileInfoList& newState)
{
	// Note: QFileInfo::operator== does exactly what's needed
	// http://doc.qt.io/qt-5/qfileinfo.html#operator-eq-eq
	// If this changes, a custom comparator will be required

	CTimeElapsed timer(true);

	timer.start();
	decltype(_previousState) newItemsSet;
	std::copy(begin_to_end(newState), std::inserter(newItemsSet, newItemsSet.end()));
	qDebug() << "Copying new" << newState.size() << "items to std::set took" << timer.elapsed() << "ms";

	timer.start();

	const auto diff = SetOperations::calculateDiff(_previousState, newItemsSet);
	qDebug() << "calculateDiff for" << _previousState.size() << "and" << newState.size() << "took" << timer.elapsed() << "ms";

	timer.start();

	std::set<QFileInfo> changedItems;
	for (const auto& newItem : diff.common_elements)
	{
		const auto sameOldItem = container_aware_find(_previousState, newItem);
		assert(sameOldItem != _previousState.end());
		if (fileInfoChanged(*sameOldItem, newItem))
			changedItems.insert(newItem);
	}

	qDebug() << "Detecting changed items for" << diff.common_elements.size() << "took" << timer.elapsed() << "ms";

	if (!changedItems.empty() || !diff.elements_from_a_not_in_b.empty() || !diff.elements_from_b_not_in_a.empty())
	{
		for (const auto& callback : _callbacks)
			callback(diff.elements_from_b_not_in_a, diff.elements_from_a_not_in_b, changedItems);

		_previousState = newItemsSet;
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

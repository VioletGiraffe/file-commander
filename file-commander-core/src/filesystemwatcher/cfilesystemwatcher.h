#pragma once

#include "compiler/compiler_warnings_control.h"
#include "container/std_container_helpers.hpp"
#include "container/ordered_containers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QFileInfo>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <deque>
#include <functional>
#include <mutex>
#include <set>

struct BasicFileSystemItemInfo
{
	QString fullPath;
	qint64 size;
	uint modificationTime;

	inline BasicFileSystemItemInfo(const QFileInfo& fullInfo) : fullPath(fullInfo.absoluteFilePath()), size(fullInfo.size()), modificationTime(fullInfo.lastModified().toTime_t()) {}

	inline bool operator<(const BasicFileSystemItemInfo& other) const {
		return fullPath < other.fullPath;
	}

	inline bool operator<(const QFileInfo& fullInfo) const {
		return fullPath < fullInfo.absoluteFilePath();
	}

	inline bool operator==(const BasicFileSystemItemInfo& other) const {
		return fullPath == other.fullPath;
	}

	inline bool operator==(const QFileInfo& fullInfo) const {
		return fullPath == fullInfo.absoluteFilePath();
	}

	inline operator QFileInfo() const {
		return QFileInfo(fullPath);
	}

	inline bool fileDetailsChanged(const QFileInfo& otherFullInfo) const {
		return size != otherFullInfo.size() || modificationTime != otherFullInfo.lastModified().toTime_t();
	}
};

using ChangeDetectedCallback = std::function<void(const transparent_set<QFileInfo>& added, const transparent_set<QFileInfo>& removed, const transparent_set<QFileInfo>& changed)>;

namespace detail {

class CFileSystemWatcherInterface
{
public:
	virtual ~CFileSystemWatcherInterface() = default;

	void addCallback(ChangeDetectedCallback callback);
	virtual bool setPathToWatch(const QString& path) = 0;

protected:
	void processChangesAndNotifySubscribers(const QFileInfoList& newState);

protected:
	std::recursive_mutex _pathMutex;
	QString _pathToWatch;

private:
	std::deque<ChangeDetectedCallback> _callbacks;
	transparent_set<BasicFileSystemItemInfo> _previousState;
};

}

inline bool operator<(const QFileInfo& lItem, const QFileInfo& rItem)
{
	return lItem.absoluteFilePath() < rItem.absoluteFilePath();
}

class CFileSystemWatcher : public detail::CFileSystemWatcherInterface
{
public:
	CFileSystemWatcher();

	bool setPathToWatch(const QString &path) override;

private:
	void onCheckForChanges();

private:
	QTimer _timer;
};

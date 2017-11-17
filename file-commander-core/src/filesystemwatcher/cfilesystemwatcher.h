#pragma once

#include <QFileInfo>
#include <QTimer>

#include <deque>
#include <functional>

using ChangeDetectedCallback = std::function<void (const std::deque<QFileInfo>& added, const std::deque<QFileInfo>& removed, const std::deque<QFileInfo>& changed)>;

namespace detail {

class CFileSystemWatcherInterface
{
public:
	void addCallback(ChangeDetectedCallback callback);
	virtual bool setPathToWatch(const QString& path) = 0;

protected:
	void processChangesAndNotifySubscribers(const QFileInfoList& newState);

protected:
	QString _pathToWatch;

private:
	std::deque<ChangeDetectedCallback> _callbacks;
	std::deque<QFileInfo> _previousState;
};

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

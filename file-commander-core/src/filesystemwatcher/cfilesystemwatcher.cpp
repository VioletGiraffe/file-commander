#include "cfilesystemwatcher.h"


// Submodule includes
#include "qt_paths.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <utility>

void CFileSystemWatcher::setPathToWatch(const QString& path)
{
	_watchedPath = path;
	_notifications.reset();
	_rewatchPending = false;
	if (path.isEmpty())
		_poller.setPathToWatch({});
	else
		watchCurrentPath();
}

void CFileSystemWatcher::captureBaselineState()
{
	_poller.captureBaselineState();
}

bool CFileSystemWatcher::changesDetected()
{
	const bool polledChange = _poller.changesDetected();
	if (_rewatchPending)
	{
		auto notifications = openNotifications();
		if (!notifications)
			return polledChange;

		_rewatchPending = false;
		useNotifications(std::move(*notifications));
		return true; // Nothing reported the changes made while the folder was unwatched
	}

	if (!_notifications)
		return polledChange;

	const auto discarded = _notifications->discard_events();
	if (!discarded) [[unlikely]]
	{
		pollInsteadOfNotifications(discarded.error());
		return true;
	}
	if (!discarded->stop)
		return polledChange || discarded->any;

	const bool polled = _notifications->coverage() == thin_io::watch_coverage::partial;
	_notifications.reset();
	switch (discarded->stop->reason)
	{
	case thin_io::stop_reason::root_replaced:
		watchCurrentPath();
		break;
	case thin_io::stop_reason::root_lost:
		// Watched again once a folder is back at the path; a running poller reports its return by itself.
		// Meanwhile the caller's refresh finds the folder gone and moves on.
		_rewatchPending = !polled;
		break;
	case thin_io::stop_reason::volume_released: // Watching again could hold the volume during an eject
		break;
	case thin_io::stop_reason::failed:
		pollInsteadOfNotifications(discarded->stop->error);
		break;
	}

	return true;
}

thin_io::filesystem_result<thin_io::directory_watcher> CFileSystemWatcher::openNotifications() const
{
	return thin_io::directory_watcher::watch(thin_io::native_path(_watchedPath).data(), { .recursive = false });
}

void CFileSystemWatcher::useNotifications(thin_io::directory_watcher&& notifications)
{
	_poller.setPathToWatch(notifications.coverage() == thin_io::watch_coverage::partial ? _watchedPath : QString{});
	_notifications = std::move(notifications);
}

void CFileSystemWatcher::watchCurrentPath()
{
	if (auto notifications = openNotifications())
		useNotifications(std::move(*notifications));
	else
		pollInsteadOfNotifications(notifications.error());
}

void CFileSystemWatcher::pollInsteadOfNotifications(const thin_io::filesystem_error& error)
{
	qInfo() << "Polling" << _watchedPath << "for changes, change notifications failed:" << QString::fromStdString(thin_io::format_filesystem_error(error));
	_notifications.reset();
	_poller.setPathToWatch(_watchedPath);
}

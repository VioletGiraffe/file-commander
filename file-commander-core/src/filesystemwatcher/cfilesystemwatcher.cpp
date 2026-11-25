#include "cfilesystemwatcher.h"

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "paths/pathsyntax.h"
#include "qt_paths.hpp" // thin_io
#include "qtcore_helpers/qstring_hash.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QDebug>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <utility>

void CFileSystemWatcher::setPathToWatch(const QString& path, const bool recursive)
{
	_watchedPath = path;
	_recursive = recursive;
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
		return rewatchIfFolderIsBack() || polledChange;

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

	return endNotifications(*discarded->stop);
}

CFileSystemWatcher::Changes CFileSystemWatcher::takeChanges()
{
	Changes changes{ .rescan = _poller.changesDetected() };
	if (_rewatchPending)
		changes.rescan = rewatchIfFolderIsBack() || changes.rescan;
	else if (_notifications)
	{
		const auto events = _notifications->read_events();
		if (!events) [[unlikely]]
		{
			pollInsteadOfNotifications(events.error());
			changes.rescan = true;
		}
		else if (!collectChangedPaths(*events, changes))
			return {};
	}

	if (changes.rescan)
		changes.paths.clear();
	return changes;
}

bool CFileSystemWatcher::notificationsReportEveryChange() const
{
	return _notifications && _notifications->coverage() == thin_io::watch_coverage::complete;
}

bool CFileSystemWatcher::collectChangedPaths(const std::vector<thin_io::watch_event>& events, Changes& changes)
{
	const QString root = withTrailingSeparator(_watchedPath);
	ankerl::unordered_dense::map<QString, size_t, QStringHash, std::equal_to<>> indexByPath;
	const auto changedPath = [&](const thin_io::native_string& relativePath) -> ChangedPath& {
		QString path = root + toPosixSeparators(thin_io::to_qstring(relativePath));
		const auto [index, inserted] = indexByPath.try_emplace(path, changes.paths.size());
		if (inserted)
			changes.paths.push_back(ChangedPath{ .path = std::move(path) });
		return changes.paths[index->second];
	};

	for (const thin_io::watch_event& event : events)
	{
		// An event about the root itself is a stop, or a change of the root's own metadata
		if (event.path.empty() && event.kind != thin_io::watch_event_kind::overflow && event.kind != thin_io::watch_event_kind::stopped)
			continue;

		const bool notKnownFile = event.entry != thin_io::entry_kind::regular_file;
		switch (event.kind)
		{
		case thin_io::watch_event_kind::created:
			changedPath(event.path).contentsMayBeNew |= notKnownFile;
			break;
		case thin_io::watch_event_kind::removed:
			changedPath(event.path).contentsMayBeGone |= notKnownFile;
			break;
		case thin_io::watch_event_kind::modified:
		case thin_io::watch_event_kind::metadata_changed:
			(void)changedPath(event.path);
			break;
		case thin_io::watch_event_kind::renamed:
			changedPath(event.old_path).contentsMayBeGone |= notKnownFile;
			changedPath(event.path).contentsMayBeNew |= notKnownFile;
			break;
		case thin_io::watch_event_kind::overflow:
			changes.rescan = true;
			break;
		case thin_io::watch_event_kind::stopped: // The last event
			if (!endNotifications(event))
				return false;
			changes.rescan = true;
			break;
		}
	}

	return true;
}

bool CFileSystemWatcher::rewatchIfFolderIsBack()
{
	auto notifications = openNotifications();
	if (!notifications)
		return false;

	_rewatchPending = false;
	useNotifications(std::move(*notifications));
	return true;
}

bool CFileSystemWatcher::endNotifications(const thin_io::watch_event& stop)
{
	const bool polled = _notifications->coverage() == thin_io::watch_coverage::partial;
	_notifications.reset();
	switch (stop.reason)
	{
	case thin_io::stop_reason::root_replaced:
		watchCurrentPath();
		break;
	case thin_io::stop_reason::root_lost:
		// Watched again once a folder is back at the path; a running poller reports its return by itself.
		// Meanwhile the caller's refresh finds the folder gone and moves on.
		_rewatchPending = !polled;
		break;
	case thin_io::stop_reason::volume_released: // Neither watched again nor reported: either would touch the volume during an eject
		return false;
	case thin_io::stop_reason::failed:
		pollInsteadOfNotifications(stop.error);
		break;
	}

	return true;
}

thin_io::filesystem_result<thin_io::directory_watcher> CFileSystemWatcher::openNotifications() const
{
	return thin_io::directory_watcher::watch(thin_io::native_path(_watchedPath).data(), { .recursive = _recursive });
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

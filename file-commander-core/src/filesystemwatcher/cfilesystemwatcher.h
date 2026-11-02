#pragma once

#include "cdirectorysnapshotpoller.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "directory_watcher.hpp" // thin_io
#include "filesystem_error.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>

// Detects changes among one folder's entries through the system's change notifications. Polls the folder instead, or
// as well, where notifications cannot be set up or may miss changes (thin_io::watch_coverage::partial).
// setPathToWatch() and changesDetected() are only called from one thread at a time; captureBaselineState() from any.
class CFileSystemWatcher
{
public:
	// An empty path releases the watch.
	void setPathToWatch(const QString& path);
	// See CDirectorySnapshotPoller::captureBaselineState(); does nothing while the folder isn't polled.
	void captureBaselineState();
	// Whether anything changed since the last call. A watch released for an eject ends silently.
	[[nodiscard]] bool changesDetected();

private:
	[[nodiscard]] thin_io::filesystem_result<thin_io::directory_watcher> openNotifications() const;
	void useNotifications(thin_io::directory_watcher&& notifications);
	void watchCurrentPath();
	void pollInsteadOfNotifications(const thin_io::filesystem_error& error);

private:
	QString _watchedPath;
	std::optional<thin_io::directory_watcher> _notifications;
	CDirectorySnapshotPoller _poller;
	// The folder left its path on a local volume: notifications are retried on every changesDetected() until it is back.
	bool _rewatchPending = false;
};

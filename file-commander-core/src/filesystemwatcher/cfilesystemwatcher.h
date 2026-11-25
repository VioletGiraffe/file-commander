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
#include <vector>

// Detects changes among one folder's entries, or in the whole tree under it, through the system's change notifications.
// A tree's watch follows its folder links, as the flattened listing does.
// Polls the folder instead, or as well, where notifications cannot be set up or may miss changes (thin_io::watch_coverage::partial);
// the poll never covers subfolders.
// Every call but captureBaselineState() and prepareTreeWatch() comes from one thread at a time; those two from any.
class CFileSystemWatcher
{
public:
	struct ChangedPath
	{
		QString path; // Absolute, '/'-separated, without a trailing separator
		bool contentsMayBeNew = false;  // Created or renamed to: a folder here may hold entries no event reports
		bool contentsMayBeGone = false; // Removed or renamed from, not known to be a file: entries under it may be gone unreported
	};

	struct Changes
	{
		bool rescan = false; // Changes may have gone unreported: the whole tree must be listed again. paths is then empty.
		std::vector<ChangedPath> paths; // Each path once
	};

	// A tree's watch armed on any thread, for setPreparedTreeWatch(): arming walks the whole tree
	struct PreparedTreeWatch
	{
		QString path;
		thin_io::filesystem_result<thin_io::directory_watcher> notifications;
		// Where the folder is polled: the poll's baseline, taken with the watch, before the listing
		std::optional<thin_io::directory_snapshot> pollBaseline;
	};

	// Watches the folder's own entries. An empty path releases the watch.
	void setPathToWatch(const QString& path);
	[[nodiscard]] static PreparedTreeWatch prepareTreeWatch(const QString& path);
	// Watches the tree. A re-arm after the folder is lost or replaced walks it on the calling thread.
	void setPreparedTreeWatch(PreparedTreeWatch&& watch);
	// See CDirectorySnapshotPoller::captureBaselineState(); does nothing while the folder isn't polled.
	void captureBaselineState();
	// Whether anything changed since the last call. A watch released for an eject ends silently.
	[[nodiscard]] bool changesDetected();
	// The changes since the last call, path by path. A watch released for an eject ends silently.
	[[nodiscard]] Changes takeChanges();
	// False while a change may go unreported: no notifications, or notifications with partial coverage
	[[nodiscard]] bool notificationsReportEveryChange() const;

private:
	[[nodiscard]] static thin_io::filesystem_result<thin_io::directory_watcher> openNotifications(const QString& path, bool recursive);
	void useNotifications(thin_io::directory_watcher&& notifications);
	void watchCurrentPath();
	void useNotificationsOrPoll(thin_io::filesystem_result<thin_io::directory_watcher>&& notifications);
	// Whether useNotificationsOrPoll() polls the folder as well
	[[nodiscard]] static bool isPolledWith(const thin_io::filesystem_result<thin_io::directory_watcher>& notifications);
	void pollInsteadOfNotifications(const thin_io::filesystem_error& error);
	// True when the watch is armed again: nothing reported the changes made while the folder was unwatched
	[[nodiscard]] bool rewatchIfFolderIsBack();
	// Re-arms or polls as the stop's reason requires. False when the end must not count as a change.
	[[nodiscard]] bool endNotifications(const thin_io::watch_event& stop);
	// False when the events end with a stop that must not count as a change
	[[nodiscard]] bool collectChangedPaths(const std::vector<thin_io::watch_event>& events, Changes& changes);

private:
	QString _watchedPath;
	bool _recursive = false;
	std::optional<thin_io::directory_watcher> _notifications;
	CDirectorySnapshotPoller _poller;
	// The folder left its path on a local volume: notifications are retried on every changesDetected() or takeChanges() until it is back.
	bool _rewatchPending = false;
};

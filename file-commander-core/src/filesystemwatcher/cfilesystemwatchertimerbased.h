#include "threading/cperiodicexecutionthread.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <stdint.h>
#include <mutex>
#include <set>

struct FileSystemInfoWrapper
{
	QFileInfo _info;

	explicit FileSystemInfoWrapper(QFileInfo&& fullInfo) noexcept;

	[[nodiscard]] bool operator<(const FileSystemInfoWrapper& other) const noexcept;
	[[nodiscard]] bool operator==(const FileSystemInfoWrapper& other) const noexcept;

	[[nodiscard]] qint64 size() const noexcept;

private:
	QString _itemName;
	mutable qint64 _size = -1;
};

class CFileSystemWatcherTimerBased
{
public:
	CFileSystemWatcherTimerBased();
	~CFileSystemWatcherTimerBased();

	CFileSystemWatcherTimerBased(const CFileSystemWatcherTimerBased&) = delete;
	CFileSystemWatcherTimerBased& operator=(const CFileSystemWatcherTimerBased&) = delete;

	// This method is thread-safe.
	bool setPathToWatch(const QString &path);
	// The poll loop takes its baseline up to a period after setPathToWatch, so a change made in that gap is
	// absorbed into the baseline and never reported. Call this when you take your own snapshot of the folder, to
	// pin the baseline to that moment instead. Scans synchronously on the calling thread.
	void captureBaselineState();
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void onCheckForChanges();
	[[nodiscard]] static std::set<FileSystemInfoWrapper> snapshotDirectory(const QString& path);
	void processChangesAndNotifySubscribers(std::set<FileSystemInfoWrapper>&& newState, uint64_t pathGeneration);

private:
	CPeriodicExecutionThread _periodicThread{ 400 /* period in ms*/, "CFileSystemWatcher thread" };
	// Written by the poll thread and by captureBaselineState() (panel worker thread); all access under _mutex.
	std::set<FileSystemInfoWrapper> _previousState;
	uint64_t _previousStateGeneration = 0;

	std::recursive_mutex _mutex;
	QString _pathToWatch;
	uint64_t _pathGeneration = 0;

	std::atomic_bool _bChangeDetected = false;
};

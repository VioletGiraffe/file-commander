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
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void onCheckForChanges();
	void processChangesAndNotifySubscribers(QFileInfoList&& newState, uint64_t pathGeneration);

private:
	CPeriodicExecutionThread _periodicThread{ 400 /* period in ms*/, "CFileSystemWatcher thread" };
	// Accessed only by the periodic thread. A generation mismatch makes the next completed scan a silent baseline.
	std::set<FileSystemInfoWrapper> _previousState;
	uint64_t _previousStateGeneration = 0;

	std::recursive_mutex _mutex;
	QString _pathToWatch;
	uint64_t _pathGeneration = 0;

	std::atomic_bool _bChangeDetected = false;
};

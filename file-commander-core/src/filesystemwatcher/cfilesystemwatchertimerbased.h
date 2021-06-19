#include "cfilesystemwatcherinterface.h"
#include "threading/cperiodicexecutionthread.h"

#include <set>

struct FileSystemInfoWrapper
{
	QFileInfo _info;

	explicit FileSystemInfoWrapper(QFileInfo&& fullInfo) noexcept;

	FileSystemInfoWrapper(FileSystemInfoWrapper&&) noexcept = default;
	FileSystemInfoWrapper(const FileSystemInfoWrapper& other) noexcept = default;

	[[nodiscard]] bool operator<(const FileSystemInfoWrapper& other) const noexcept;
	[[nodiscard]] bool operator==(const FileSystemInfoWrapper& other) const noexcept;

	[[nodiscard]] qint64 size() const noexcept;

private:
	QString _itemName;
	mutable qint64 _size = -1;
};

class CFileSystemWatcherTimerBased final : public detail::CFileSystemWatcherInterface
{
public:
	CFileSystemWatcherTimerBased();
	~CFileSystemWatcherTimerBased();

	bool setPathToWatch(const QString &path) override;

private:
	void onCheckForChanges();
	void processChangesAndNotifySubscribers(QFileInfoList&& newState);

private:
	CPeriodicExecutionThread _periodicThread{ 400 /* period in ms*/, "CFileSystemWatcher thread" };
	std::set<FileSystemInfoWrapper> _previousState;
};

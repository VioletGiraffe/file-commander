#include "cfilesystemwatcherinterface.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

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
	[[nodiscard]] uint modificationTime() const noexcept;

private:
	QString _fullPath;
	mutable qint64 _size = -1;
	mutable uint _modificationTime = 0;
};

class CFileSystemWatcherTimerBased final : public detail::CFileSystemWatcherInterface
{
public:
	CFileSystemWatcherTimerBased();

	bool setPathToWatch(const QString &path) override;

private:
	void onCheckForChanges();
	void processChangesAndNotifySubscribers(QFileInfoList&& newState);

private:
	std::set<FileSystemInfoWrapper> _previousState;
	QTimer _timer;
};

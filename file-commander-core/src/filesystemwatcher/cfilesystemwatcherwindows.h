#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "filesystem_types.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QAbstractNativeEventFilter>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <mutex>
#include <optional>

using HANDLE = void*;

class CFileSystemWatcherWindows final : public QAbstractNativeEventFilter
{
public:
	CFileSystemWatcherWindows() noexcept;
	~CFileSystemWatcherWindows() noexcept;

	CFileSystemWatcherWindows(const CFileSystemWatcherWindows&) = delete;
	CFileSystemWatcherWindows& operator=(const CFileSystemWatcherWindows&) = delete;

	// This method is thread-safe.
	bool setPathToWatch(const QString &path) noexcept;
	// The native handle set up by setPathToWatch already reports every change from arm time on - explicit call is no-op
	inline void captureBaselineState() noexcept {}
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	bool armDirectoryWatch() noexcept;
	void close() noexcept;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
#else
	bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;
#endif

private:
	std::mutex _mtx;
	QString _watchedPath;
	HANDLE _directoryHandle = nullptr; // Changes inside the directory
	// Directory renames, moves and deletions in the parent: the directory handle never reports its own rename or move.
	HANDLE _parentHandle = nullptr;
	// What _directoryHandle watches: a different identity at _watchedPath means the directory was replaced.
	std::optional<thin_io::entry_identity> _directoryId;
	bool _volumeRemoved = false;
};

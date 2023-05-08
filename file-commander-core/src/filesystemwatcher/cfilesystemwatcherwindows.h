#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractNativeEventFilter>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <mutex>

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
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void close() noexcept;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
#else
	bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;
#endif

private:
	std::mutex _mtx;
	QString _watchedPath;
	HANDLE _handle = nullptr;
	bool _volumeRemoved = false;
};

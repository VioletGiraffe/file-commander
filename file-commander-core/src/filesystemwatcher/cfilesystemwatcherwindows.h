#pragma once

#include <mutex>

class QString;
using HANDLE = void*;

class CFileSystemWatcherWindows
{
public:
	~CFileSystemWatcherWindows() noexcept;

	// This method is thread-safe.
	bool setPathToWatch(const QString &path) noexcept;
	// Poll this function to find out if there were any changes since the last check.
	// This method is thread-safe.
	bool changesDetected() noexcept;

private:
	void close() noexcept;

private:
	std::mutex _mtx;
	HANDLE _handle = nullptr;
};

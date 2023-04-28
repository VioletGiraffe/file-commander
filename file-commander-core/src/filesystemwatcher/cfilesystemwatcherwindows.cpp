#include "cfilesystemwatcherwindows.h"
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <Windows.h>

CFileSystemWatcherWindows::~CFileSystemWatcherWindows() noexcept
{
	std::lock_guard lock{ _mtx };
	close();
}

bool CFileSystemWatcherWindows::setPathToWatch(const QString& path) noexcept
{
	std::lock_guard lock{ _mtx };
	close();

	if (path.isEmpty())
		return true;

	WCHAR wPath[32768];
	const auto pathLen = path.toWCharArray(wPath);
	if (pathLen <= 0)
		return false;

	assert_debug_only(!path.contains('\\'));
	wPath[pathLen] = 0;
	if (path.endsWith('/'))
		wPath[pathLen - 1] = 0; // FindFirstChangeNotificationW fails if the trailing slash is present!

	_handle = ::FindFirstChangeNotificationW(wPath, FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE);
	if (_handle == INVALID_HANDLE_VALUE)
	{
		_handle = nullptr;
		return false;
	}

	assert_debug_only(_handle != nullptr);
	return true;
}

bool CFileSystemWatcherWindows::changesDetected() noexcept
{
	std::lock_guard lock{ _mtx };
	if (_handle == nullptr)
		return false;

	const auto result = ::WaitForSingleObject(_handle, 0 /* no wait */);
	if (result != WAIT_OBJECT_0)
		return false; // Handle not signaled - no change detected

	// Change detected - reset the handle
	assert_r(::FindNextChangeNotification(_handle));
	return true;
}

void CFileSystemWatcherWindows::close() noexcept
{
	if (_handle != nullptr)
	{
		assert_r(::FindCloseChangeNotification(_handle));
		_handle = nullptr;
	}
}

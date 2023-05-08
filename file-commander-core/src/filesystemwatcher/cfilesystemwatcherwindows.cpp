#include "cfilesystemwatcherwindows.h"
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

#include <Windows.h>
#include <Dbt.h>

CFileSystemWatcherWindows::CFileSystemWatcherWindows() noexcept
{
	qApp->installNativeEventFilter(this);
}

CFileSystemWatcherWindows::~CFileSystemWatcherWindows() noexcept
{
	std::lock_guard lock{ _mtx };
	qApp->removeNativeEventFilter(this);
	close();
}

bool CFileSystemWatcherWindows::setPathToWatch(const QString& path) noexcept
{
	std::lock_guard lock{ _mtx };
	close();

	_watchedPath = path;
	_volumeRemoved = false;

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

	if (_volumeRemoved) [[unlikely]]
	{
		_volumeRemoved = false;
		return true;
	}

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool CFileSystemWatcherWindows::nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/)
#else
bool CFileSystemWatcherWindows::nativeEventFilter(const QByteArray& /*eventType*/, void* message, long* /*result*/)
#endif
{
	MSG* winMsg = static_cast<MSG*>(message);
	if (winMsg->message != WM_DEVICECHANGE || winMsg->wParam != DBT_DEVICEREMOVECOMPLETE)
		return false;

	PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)winMsg->lParam;
	if (lpdb->dbch_devicetype != DBT_DEVTYP_VOLUME)
		return false;

	PDEV_BROADCAST_VOLUME lpdbv = (PDEV_BROADCAST_VOLUME)lpdb;
	DWORD mask = lpdbv->dbcv_unitmask;
	assert_r(mask != 0);

	std::lock_guard lock{ _mtx };

	for (DWORD i = 0; i < 26; ++i)
	{
		if (mask & 0x1)
		{
			const char letter = 'A' + (char)i;
			if (_watchedPath.startsWith(letter))
			{
				_volumeRemoved = true;
				break;
			}
		}
		mask >>= 1;
	}

	return false;
}

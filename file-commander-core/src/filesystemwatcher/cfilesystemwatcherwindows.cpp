#include "cfilesystemwatcherwindows.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"
#include "directoryscan/thinioqstring.h" // resolvedObjectId, withoutTrailingSeparator
#include "paths/pathsyntax.h"
#include "windows_path_win.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QFileInfo>
RESTORE_COMPILER_WARNINGS

#include <Windows.h>
#include <Dbt.h>

#include <utility>

CFileSystemWatcherWindows::CFileSystemWatcherWindows() noexcept
{
	QCoreApplication::instance()->installNativeEventFilter(this);
}

CFileSystemWatcherWindows::~CFileSystemWatcherWindows() noexcept
{
	std::lock_guard lock{ _mtx };
	QCoreApplication::instance()->removeNativeEventFilter(this);
	close();
}

// Null on failure
[[nodiscard]] static HANDLE openChangeNotification(const QString& directoryPath, const DWORD filter) noexcept
{
	// FindFirstChangeNotificationW fails on a trailing separator, which a drive root nonetheless requires.
	const QString pathWithoutSeparator = withoutTrailingSeparator(directoryPath);
	const thin_io::windows_path_buffer nativePath{ reinterpret_cast<const wchar_t*>(pathWithoutSeparator.utf16()) };
	if (!nativePath)
		return nullptr;

	const HANDLE handle = ::FindFirstChangeNotificationW(nativePath.c_str(), FALSE, filter);
	return handle != INVALID_HANDLE_VALUE ? handle : nullptr;
}

static void closeChangeNotification(HANDLE& handle) noexcept
{
	if (handle == nullptr)
		return;

	assert_r(::FindCloseChangeNotification(handle));
	handle = nullptr;
}

// Re-arms a signaled handle. Closes it instead when re-arming fails, as it does once the watched directory is deleted.
[[nodiscard]] static bool consumeChangeNotification(HANDLE& handle) noexcept
{
	if (handle == nullptr || ::WaitForSingleObject(handle, 0 /* no wait */) != WAIT_OBJECT_0)
		return false;

	if (!::FindNextChangeNotification(handle))
		closeChangeNotification(handle);

	return true;
}

bool CFileSystemWatcherWindows::setPathToWatch(const QString& path) noexcept
{
	std::lock_guard lock{ _mtx };
	close();

	_watchedPath = path;
	_volumeRemoved = false;

	if (path.isEmpty())
		return true;

	assert_debug_only(!path.contains('\\'));

	// Armed before the directory watch, so a replacement made in between still signals.
	if (const QString directoryPath = withoutTrailingSeparator(path); !isRootPath(directoryPath))
		_parentHandle = openChangeNotification(QFileInfo{ directoryPath }.path(), FILE_NOTIFY_CHANGE_DIR_NAME);

	return armDirectoryWatch();
}

bool CFileSystemWatcherWindows::changesDetected() noexcept
{
	std::lock_guard lock{ _mtx };
	bool changed = std::exchange(_volumeRemoved, false);
	if (consumeChangeNotification(_directoryHandle))
		changed = true;

	// The parent is re-armed before the identity check, so a replacement made after the check signals again.
	if (consumeChangeNotification(_parentHandle))
	{
		// An unknown identity never matches: see resolvedObjectId.
		if (const auto currentId = resolvedObjectId(_watchedPath); !currentId || currentId != _directoryId)
		{
			closeChangeNotification(_directoryHandle);
			armDirectoryWatch();
			changed = true;
		}
	}

	return changed;
}

// Requires _mtx
bool CFileSystemWatcherWindows::armDirectoryWatch() noexcept
{
	// The identity is read before the handle is opened: a replacement in between then fails the next identity check.
	_directoryId = resolvedObjectId(_watchedPath);
	_directoryHandle = openChangeNotification(_watchedPath, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE);
	return _directoryHandle != nullptr;
}

void CFileSystemWatcherWindows::close() noexcept
{
	closeChangeNotification(_directoryHandle);
	closeChangeNotification(_parentHandle);
	_directoryId.reset();
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

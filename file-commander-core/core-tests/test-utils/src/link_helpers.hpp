#pragma once

// Creation of the platform's link types, for tests that need a real link on disk.

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QProcess>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#include <winioctl.h>
#else
#include <unistd.h>
#endif

inline bool createHardLink(const QString& existingPath, const QString& linkPath)
{
#ifdef _WIN32
	return CreateHardLinkW(reinterpret_cast<const WCHAR*>(linkPath.utf16()), reinterpret_cast<const WCHAR*>(existingPath.utf16()), nullptr) != 0;
#else
	return ::link(QFile::encodeName(existingPath).constData(), QFile::encodeName(linkPath).constData()) == 0;
#endif
}

// Symlink creation primitives. Both store the target string verbatim, so a relative target resolves against
// the link's own directory. QFile::link() is not used: on Windows it writes a .lnk shortcut rather than a
// reparse point, and nowhere does it promise to keep a relative target relative.
#ifdef _WIN32
// The two symlink flavors differ only in the directory flag. ALLOW_UNPRIVILEGED_CREATE is what lets the call
// succeed in Developer Mode; without it only an elevated process may create a symlink at all.
inline bool createWindowsSymlink(const QString& targetPath, const QString& linkPath, const DWORD kindFlag)
{
	const QString nativeLink = QDir::toNativeSeparators(linkPath);
	const QString nativeTarget = QDir::toNativeSeparators(targetPath);
	return CreateSymbolicLinkW(reinterpret_cast<const WCHAR*>(nativeLink.utf16()), reinterpret_cast<const WCHAR*>(nativeTarget.utf16()),
		kindFlag | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0;
}
#else
inline bool createPosixSymlink(const QString& targetPath, const QString& linkPath)
{
	return ::symlink(QFile::encodeName(targetPath).constData(), QFile::encodeName(linkPath).constData()) == 0;
}
#endif

// Creates the platform's common directory link type: a symlink on POSIX, a junction on Windows
// (junction creation, unlike symlink creation, requires no special privileges on Windows).
// A junction's target is resolved and stored absolute at creation, so it cannot express a relative target.
inline bool createDirectoryLink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return QProcess::execute(QStringLiteral("cmd"), { QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
		QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(targetPath) }) == 0;
#else
	return createPosixSymlink(targetPath, linkPath);
#endif
}

// A symbolic link to a file. On Windows this is an IO_REPARSE_TAG_SYMLINK reparse point, and creating one is
// privileged - symlinkCreationUnavailable() is how tests decide what to do when it fails for that reason.
inline bool createFileSymlink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return createWindowsSymlink(targetPath, linkPath, 0);
#else
	return createPosixSymlink(targetPath, linkPath);
#endif
}

// A symbolic link to a directory. On Windows this carries a different reparse tag than the junction
// createDirectoryLink() makes, so the two are not interchangeable as test inputs; on POSIX they are one thing.
inline bool createDirectorySymlink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return createWindowsSymlink(targetPath, linkPath, SYMBOLIC_LINK_FLAG_DIRECTORY);
#else
	return createPosixSymlink(targetPath, linkPath);
#endif
}

#ifdef _WIN32
// Makes an existing file a reparse point that is not a link, as a cloud placeholder is: a third-party tag
// without the name-surrogate bit. A non-Microsoft tag requires the GUID buffer layout.
inline bool setNonLinkReparsePoint(const QString& filePath)
{
	const QString nativePath = QDir::toNativeSeparators(filePath);
	const HANDLE handle = CreateFileW(reinterpret_cast<const WCHAR*>(nativePath.utf16()), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
		FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
	if (handle == INVALID_HANDLE_VALUE)
		return false;

	REPARSE_GUID_DATA_BUFFER reparse{};
	reparse.ReparseTag = 0x00007A11;
	reparse.ReparseDataLength = 1;
	reparse.ReparseGuid = { 0x6f1c3a52, 0x1d4e, 0x4b8a, { 0x9c, 0x31, 0x2e, 0x7d, 0x5a, 0x40, 0x8b, 0x16 } };
	DWORD ignored = 0;
	const bool set = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &reparse, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE + reparse.ReparseDataLength,
		nullptr, 0, &ignored, nullptr) != 0;
	CloseHandle(handle);
	return set;
}
#endif

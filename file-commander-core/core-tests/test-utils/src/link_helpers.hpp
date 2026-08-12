#pragma once

// Creation of the platform's link types, for tests that need a real link on disk.

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QProcess>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
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

// Creates the platform's common directory link type: a symlink on POSIX, a junction on Windows
// (junction creation, unlike symlink creation, requires no special privileges on Windows)
inline bool createDirectoryLink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return QProcess::execute(QStringLiteral("cmd"), { QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
		QDir::toNativeSeparators(linkPath), QDir::toNativeSeparators(targetPath) }) == 0;
#else
	return QFile::link(targetPath, linkPath);
#endif
}

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
#endif

// A symbolic link to a file. On Windows this is an IO_REPARSE_TAG_SYMLINK reparse point, which QFile::link()
// cannot produce - there it writes a .lnk shortcut instead. Creation is privileged on Windows and may fail
// for that reason alone; symlinkCreationUnavailable() is how tests decide what to do about it.
inline bool createFileSymlink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return createWindowsSymlink(targetPath, linkPath, 0);
#else
	return QFile::link(targetPath, linkPath);
#endif
}

// A symbolic link to a directory. On Windows this carries a different reparse tag than the junction
// createDirectoryLink() makes, so the two are not interchangeable as test inputs; on POSIX they are one thing.
inline bool createDirectorySymlink(const QString& targetPath, const QString& linkPath)
{
#ifdef _WIN32
	return createWindowsSymlink(targetPath, linkPath, SYMBOLIC_LINK_FLAG_DIRECTORY);
#else
	return QFile::link(targetPath, linkPath);
#endif
}

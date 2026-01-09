#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

class CFileSystemObject;

[[nodiscard]] consteval char nativeSeparator() noexcept
{
#ifdef _WIN32
	return '\\';
#else
	return '/';
#endif
}

[[nodiscard]] consteval bool caseSensitiveFilesystem() noexcept
{
#if defined _WIN32
	return false;
#elif defined __APPLE__
	return false;
#elif defined __linux__
	return true;
#elif defined __FreeBSD__
	return true;
#else
#error "Unknown operating system"
	return true;
#endif
}

[[nodiscard]] QString toNativeSeparators(QString path);

[[nodiscard]] QString toPosixSeparators(QString path);

[[nodiscard]] QString escapedPath(QString path);

[[nodiscard]] QString cleanPath(QString path);

[[nodiscard]] QString fileSizeToString(uint64_t size, char maxUnit = '\0', const QString& spacer = {}, int significantPlaces = 4);

[[nodiscard]] std::vector<QString> pathComponents(const QString& path);

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] QString longestCommonRootPath(const QString& pathA, const QString& pathB);

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] QString longestCommonRootPath(const CFileSystemObject& object1, const CFileSystemObject& object2);

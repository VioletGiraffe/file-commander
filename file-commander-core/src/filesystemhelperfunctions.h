#pragma once
#include "compiler/compiler_warnings_control.h"

#include "filesystem_types.hpp" // thin_io: entry_identity

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

class CFileSystemObject;

// Unique identity of the filesystem entry the path resolves to (links are followed).
// Empty both when the path cannot be resolved (e. g. a broken link) and when the filesystem exposes no stable
// identity, so two empty results are never the same entry.
[[nodiscard]] std::optional<thin_io::entry_identity> resolvedObjectId(const QString& path);

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

// CFileSystemObject normalizes a directory's path to always end with a slash; native APIs are less accommodating,
// CreateFileW especially, once the \\?\ prefix has turned path normalization off. Strip it before handing a path to
// one. A root keeps its slash: "C:" names the drive's current directory rather than its root.
[[nodiscard]] QString withoutTrailingSeparator(QString path);

[[nodiscard]] QString escapedPath(QString path);

[[nodiscard]] QString cleanPath(QString path);

[[nodiscard]] QString fileSizeToString(uint64_t size, char maxUnit = '\0', const QString& spacer = {}, int significantPlaces = 4);

[[nodiscard]] std::vector<QString> pathComponents(const QString& path);

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] QString longestCommonRootPath(const QString& pathA, const QString& pathB);

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] QString longestCommonRootPath(const CFileSystemObject& object1, const CFileSystemObject& object2);

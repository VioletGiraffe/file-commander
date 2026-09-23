#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "filesystem_error.hpp" // thin_io
#include "filesystem_types.hpp" // thin_io


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

// Unique identity of the filesystem entry the path resolves to (links are followed).
// Empty both when the path cannot be resolved (e. g. a broken link) and when the filesystem exposes no stable
// identity, so two empty results are never the same entry.
[[nodiscard]] std::optional<thin_io::entry_identity> resolvedObjectId(const QString& path);

// thin_io::list_directory() with listing_detail::full
[[nodiscard]] thin_io::filesystem_result<std::vector<thin_io::directory_entry>> listDirectoryWithDetails(const QString& dirPath);
[[nodiscard]] thin_io::filesystem_result<thin_io::directory_entry> getDirectoryEntry(const QString& path);
// Lossy for a POSIX name that is not valid in the locale's encoding
[[nodiscard]] QString nativeNameToQString(const thin_io::native_string& name);

// True for POSIX symlinks, and on Windows only for name-surrogate reparse points (symlinks, junctions): other reparse
// entries (OneDrive placeholders and the like) are ordinary files/directories.
[[nodiscard]] bool isLinkEntry(const thin_io::entry_attributes& attributes) noexcept;

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
// Either separator is accepted.
[[nodiscard]] QString withoutTrailingSeparator(QString path);

// Quotes only where a shell would otherwise misread the path, so an ordinary one comes back bare and stays readable when pasted.
// The trailing separator is stripped: inside the quotes it would read as an escaped quote.
// A %VAR% still expands on Windows: cmd does that inside double quotes too, out of reach of quoting.
[[nodiscard]] QString shellQuotedPath(QString path);

#ifndef _WIN32
// Splits text into words by sh's quoting and escaping rules, with no expansion: undoes shellQuotedPath.
// Empty when a quote or an escape is left unfinished.
[[nodiscard]] std::optional<QStringList> splitShellWords(const QString& text);
#endif

[[nodiscard]] QString fileSizeToString(uint64_t size, char maxUnit = '\0', const QString& spacer = {}, int significantPlaces = 4);

[[nodiscard]] std::vector<QString> pathComponents(const QString& path);

[[nodiscard]] QString longestCommonRootPath(const QString& pathA, const QString& pathB);

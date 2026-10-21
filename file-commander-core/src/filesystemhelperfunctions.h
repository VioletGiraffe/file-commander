#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>

[[nodiscard]] consteval char nativeSeparator() noexcept
{
#ifdef _WIN32
	return '\\';
#else
	return '/';
#endif
}

[[nodiscard]] QString toNativeSeparators(QString path);

[[nodiscard]] QString toPosixSeparators(QString path);

// Typed path text made absolute: environment variables expanded, then the text resolved against baseDir, an absolute folder.
// An undefined variable stays as typed; on POSIX no variable in the text is expanded then.
// Windows: a drive letter without a separator ("E:", "E:dir") means that drive's root; "/dir" starts at baseDir's root.
// "." and ".." are resolved lexically. Empty when the expanded text is, and on Windows for a UNC spelling without a share.
[[nodiscard]] QString resolveUserPath(const QString& text, const QString& baseDir);

// Quotes only where a shell would otherwise misread the path, so an ordinary one comes back bare and stays readable when pasted.
// The trailing separator is stripped: inside the quotes it would read as an escaped quote.
// A %VAR% still expands on Windows: cmd does that inside double quotes too, out of reach of quoting.
[[nodiscard]] QString shellQuotedPath(QString path);

#ifndef _WIN32
// Splits text into words by sh's quoting and escaping rules, with no expansion: undoes shellQuotedPath.
// Empty when a quote or an escape is left unfinished.
[[nodiscard]] std::optional<QStringList> splitShellWords(const QString& text);

// Device ID of the filesystem holding the path, or of its nearest existing ancestor. Empty when it cannot be queried.
[[nodiscard]] std::optional<uint64_t> deviceIdForPath(const QString& fullPath);
#endif

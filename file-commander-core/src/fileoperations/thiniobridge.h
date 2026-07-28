#pragma once

// Module-private glue between CEntryPath and the thin_io native path and error surfaces.
// NativePathString must stay small: buildNode() holds one across its recursion, so an inline 32K path buffer
// would overflow the stack a few directory levels down.

#include "centrypath.h"

#include "filesystem_error.hpp" // thin_io
#include "filesystem_types.hpp" // thin_io: native_string

#include "assert/advanced_assert.h"

#include <optional>

// Native APIs consume NUL-terminated strings, so an embedded NUL would address only a prefix. CEntryPath rejects
// it at construction; this last shared boundary makes any future invariant breach fail loudly in development.
inline const QString& nativePathValue(const CEntryPath& path)
{
	assert_debug_only(!path.value().contains(QChar{ 0 }));
	return path.value();
}

#ifdef _WIN32

#include <string>

using NativePathString = std::wstring;

inline NativePathString thinIoPath(const CEntryPath& path)
{
	return nativePathValue(path).toStdWString();
}

inline const wchar_t* nativeCStr(const NativePathString& path)
{
	return path.c_str();
}

inline std::optional<QString> decodeNativeNameLosslessly(const thin_io::native_string& name)
{
	return QString::fromStdWString(name);
}

#else

DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

using NativePathString = QByteArray;

inline NativePathString thinIoPath(const CEntryPath& path)
{
	return QFile::encodeName(nativePathValue(path));
}

inline const char* nativeCStr(const NativePathString& path)
{
	return path.constData();
}

inline std::optional<QString> decodeNativeNameLosslessly(const thin_io::native_string& name)
{
	const QByteArray nativeName{ name.data(), static_cast<qsizetype>(name.size()) };
#ifdef __APPLE__
	// QFile's Darwin codec deliberately normalizes Unicode, so its encoded bytes need not match the
	// directory spelling. Validate the UTF-8 conversion itself before applying that normalization.
	const QString decodedUtf8 = QString::fromUtf8(nativeName);
	if (decodedUtf8.toUtf8() != nativeName)
		return std::nullopt;
	return QFile::decodeName(nativeName);
#else
	const QString decoded = QFile::decodeName(nativeName);
	if (QFile::encodeName(decoded) != nativeName)
		return std::nullopt;
	return decoded;
#endif
}

#endif

// Must be called immediately after the failing native call, before anything can overwrite the thread-local error state.
inline thin_io::filesystem_error_code captureNativeError() noexcept
{
	return thin_io::capture_last_filesystem_error().native_code;
}

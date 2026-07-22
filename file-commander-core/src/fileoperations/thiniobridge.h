#pragma once

// Module-private glue between CEntryPath and the thin_io native path and error surfaces.

#include "centrypath.h"

#include "filesystem_error.hpp" // thin_io
#include "filesystem_types.hpp" // thin_io: native_string

#ifdef _WIN32

#include <string>

using NativePathString = std::wstring;

inline NativePathString thinIoPath(const CEntryPath& path)
{
	return path.value().toStdWString();
}

inline const wchar_t* nativeCStr(const NativePathString& path)
{
	return path.c_str();
}

inline QString fromNativeName(const thin_io::native_string& name)
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
	return QFile::encodeName(path.value());
}

inline const char* nativeCStr(const NativePathString& path)
{
	return path.constData();
}

inline QString fromNativeName(const thin_io::native_string& name)
{
	return QFile::decodeName(QByteArray(name.data(), static_cast<qsizetype>(name.size())));
}

#endif

// Must be called immediately after the failing native call, before anything can overwrite the thread-local error state.
inline thin_io::filesystem_error_code captureNativeError() noexcept
{
	return thin_io::capture_last_filesystem_error().native_code;
}

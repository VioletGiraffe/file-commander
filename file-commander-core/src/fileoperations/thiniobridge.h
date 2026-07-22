#pragma once

// Module-private glue between CEntryPath and the thin_io native path and error surfaces.

#include "centrypath.h"

#include "filesystem_error.hpp" // thin_io

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

#endif

// Must be called immediately after the failing native call, before anything can overwrite the thread-local error state.
inline thin_io::filesystem_error_code captureNativeError() noexcept
{
	return thin_io::capture_last_filesystem_error().native_code;
}

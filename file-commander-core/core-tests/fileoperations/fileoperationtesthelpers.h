#pragma once

// Engine-agnostic helpers shared by all file-operation test .cpp files.
// Includes catch.hpp: the runner TU must #define CATCH_CONFIG_RUNNER before including this header.

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

#include <stdint.h>

#include "3rdparty/catch2/catch.hpp"

// Settable via the --std-seed command-line option; see main.cpp.
extern uint32_t g_randomSeed;

inline void writeTestFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

inline QByteArray readFileContents(const QString& path)
{
	QFile file(path);
	REQUIRE(file.open(QFile::ReadOnly));
	return file.readAll();
}

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

#include "filesystemhelpers.hpp"
#include "../filesystemhelperfunctions.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "assert/advanced_assert.h"
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QRegularExpression>
#include <QStringBuilder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <stdlib.h>
#include <unistd.h> // access()
#endif

// If the command exists, returns its path: either the argument as is if exists (absolute, or in the working dir),
// or based on the PATH env var.
// Returns empty string if the command's location cannot be found.
// Can properly ignore the command's arguments, if any supplied.
QString FileSystemHelpers::resolvePath(const QString &command)
{
	if (command.isEmpty())
		return {};

	if (QFile::exists(command))
		return command;

	QString commandExecutable = command.left(command.indexOf(' '));
	if (QFile::exists(commandExecutable))
		return commandExecutable;

#ifdef _WIN32
	WCHAR paths[32767];
	const auto getEnvironmentVariableW_num_characters_returned = GetEnvironmentVariableW(L"PATH", paths, 32767);
	assert_and_return_r(getEnvironmentVariableW_num_characters_returned > 0, {});

	const QStringList pathDirectories = QString::fromWCharArray(paths, getEnvironmentVariableW_num_characters_returned).split(';', Qt::SkipEmptyParts);
#else
	const QStringList pathDirectories = QString(::getenv("PATH")).split(':', Qt::SkipEmptyParts);
#endif

	for (const auto& directory: pathDirectories)
	{
		auto fullPath = directory + '/' + commandExecutable;
		if (QFile::exists(fullPath))
			return fullPath;
	}

	return {};
}

// Removes any CR/LF characters. If there are any other symbols not supported in the current file system's paths, such characters are replaced with '_' (underscore).
QString FileSystemHelpers::trimUnsupportedSymbols(QString path)
{
	static const QRegularExpression regex(QSL("[\\x{1}-\\x{1F}]+"));
	path.remove(regex);

#ifdef _WIN32
	if (path.count(':') > 1)
		path = path.mid(path.indexOf(':') + 1).replace(':', '_');
#endif

	return path;
}

bool FileSystemHelpers::pathIsAccessible(const QString& path)
{
#ifdef _WIN32
	QString pathWithMask = toNativeSeparators(path);
	if (pathWithMask.endsWith('\\'))
		pathWithMask = R"(\\?\)" % pathWithMask % '*';
	else
		pathWithMask = R"(\\?\)" % pathWithMask % "\\*";

	wchar_t wPath[32768];
	const auto nCharacters = pathWithMask.toWCharArray(wPath);
	wPath[nCharacters] = 0;

	WIN32_FIND_DATAW fileData;
	const HANDLE hFind = ::FindFirstFileExW(wPath, FindExInfoBasic, &fileData, FindExSearchNameMatch, nullptr, 0);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		const auto err = GetLastError();

		// ERROR_FILE_NOT_FOUND (2) means "no files in the specified folder", ERROR_PATH_NOT_FOUND (3) - "no such folder"
		return err == ERROR_FILE_NOT_FOUND ? true : false;
	}

	::FindClose(hFind);
	return true;
#else // not _WIN32
	return ::access(path.toLocal8Bit().constData(), R_OK) == 0;

	// Alternative method:

	//DIR* dir = opendir(path.data());

	//if (dir == nullptr)
	//	return false;

	//closedir(dir);
	//return true;
#endif
}

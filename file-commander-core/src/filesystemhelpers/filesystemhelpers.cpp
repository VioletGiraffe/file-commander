#include "filesystemhelpers.hpp"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QRegularExpression>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <stdlib.h>
#endif

// If the command exists, returns its path: either the argument as is if exists (absolute, or in the working dir),
// or based on the PATH env var.
// Returns empty string if the command's location cannot be found.
// Can properly ignore the command's arguments, if any supplied.
QString FileSystemHelpers::resolvePath(const QString &command)
{
	if (QFile::exists(command))
		return command;

	const QString commandExecutable = command.left(command.indexOf(' '));
	if (QFile::exists(commandExecutable))
		return commandExecutable;

#ifdef _WIN32
	WCHAR paths[32767];
	const auto getEnvironmentVariableW_num_characters_returned = GetEnvironmentVariableW(L"PATH", paths, 32767);
	assert_and_return_r(getEnvironmentVariableW_num_characters_returned > 0, {});

	const QStringList pathDirectories = QString::fromWCharArray(paths, getEnvironmentVariableW_num_characters_returned).split(';', QString::SkipEmptyParts);
#else
	const QStringList pathDirectories = QString(::getenv("PATH")).split(':', QString::SkipEmptyParts);
#endif

	for (const auto& directory: pathDirectories)
	{
		const auto fullPath = directory + '/' + commandExecutable;
		if (QFile::exists(fullPath))
			return fullPath;
	}

	return {};
}

// Removes any CR/LF characters. If there are any other symbols not supported in the current file system's paths, such characters are replaced with '_' (underscore).
QString FileSystemHelpers::trimUnsupportedSymbols(QString path)
{
	path.remove(QRegularExpression("[\\x{1}-\\x{1F}]+"));

#ifdef _WIN32
	if (path.count(':') > 1)
		path = path.mid(path.indexOf(':') + 1).replace(':', '_');
#endif

	return path;
}

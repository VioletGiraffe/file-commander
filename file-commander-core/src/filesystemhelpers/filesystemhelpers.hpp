#pragma once

class QString;

namespace FileSystemHelpers
{
	// If the command exists, returns its path: either the argument as is if exists (absolute, or in the working dir),
	// or based on the PATH env var.
	// Returns empty string if the command's location cannot be found.
	// Can properly ignore the command's arguments, if any were supplied.
	QString resolvePath(const QString& command);

	// Removes any CR/LF characters. If there are any other symbols not supported in the current file system's paths, such characters are replaced with '_' (underscore).
	QString trimUnsupportedSymbols(QString path);

} // namespace

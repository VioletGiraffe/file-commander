#pragma once

class QString;

namespace FileSystemHelpers
{
	// If the command exists, returns its path: either the argument as is if exists (absolute, or in the working dir),
	// or based on the PATH env var.
	// Returns empty string if the command's location cannot be found.
	// Can properly ignore the command's arguments, if any supplied.
	QString resolvePath(const QString& command);
} // namespace

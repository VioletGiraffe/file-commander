#pragma once

// Shared by the core test suites: a throwaway directory tree, and the hash a path has in a listing.

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QFile>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

// A throwaway directory tree. Paths come back POSIX-separated and without a trailing slash - the spelling
// CPanel::setPath expects, and the one it asserts on if a backslash ever creeps in.
class TempTree
{
public:
	TempTree()
	{
		REQUIRE(_dir.isValid());
		REQUIRE(!_dir.path().contains('\\'));
	}

	[[nodiscard]] QString path() const { return _dir.path(); }
	[[nodiscard]] QString path(const QString& relative) const { return _dir.path() % '/' % relative; }

	// Both return the absolute path of what they made, so a test can name it once and refer to it by value.
	QString makeDir(const QString& relative)
	{
		const QString absolute = path(relative);
		REQUIRE(QDir{}.mkpath(absolute));
		return absolute;
	}

	QString makeFile(const QString& relative, const QByteArray& contents = {})
	{
		const QString absolute = path(relative);
		QFile file{ absolute };
		REQUIRE(file.open(QFile::WriteOnly));
		REQUIRE(file.write(contents) == contents.size());
		return absolute;
	}

private:
	QTemporaryDir _dir;
};

[[nodiscard]] inline qulonglong hashOf(const QString& path)
{
	return CFileSystemObject{ path }.hash();
}

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "paths/pathsyntax.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

TEST_CASE("shellQuotedPath leaves an ordinary path bare", "[shellQuotedPath]")
{
#ifdef _WIN32
	CHECK(shellQuotedPath(QStringLiteral("C:\\Work\\file-1.txt")) == QStringLiteral("C:\\Work\\file-1.txt"));
#else
	CHECK(shellQuotedPath(QStringLiteral("/work/file-1.txt")) == QStringLiteral("/work/file-1.txt"));
#endif
}

TEST_CASE("shellQuotedPath quotes a path with a space and drops the trailing separator", "[shellQuotedPath]")
{
#ifdef _WIN32
	CHECK(shellQuotedPath(QStringLiteral("C:\\My Work\\")) == QStringLiteral("\"C:\\My Work\""));
#else
	CHECK(shellQuotedPath(QStringLiteral("/my work/")) == QStringLiteral("'/my work'"));
#endif
}

#ifndef _WIN32
TEST_CASE("splitShellWords splits on runs of whitespace", "[splitShellWords]")
{
	CHECK(splitShellWords(QStringLiteral("  a\tb \n c  ")) == QStringList{ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") });
	CHECK(splitShellWords(QString{}) == QStringList{});
	CHECK(splitShellWords(QStringLiteral("   ")) == QStringList{});
}

TEST_CASE("splitShellWords keeps single-quoted text literal", "[splitShellWords]")
{
	CHECK(splitShellWords(QStringLiteral("'a  b' '$x\\' '\"'")) == QStringList{ QStringLiteral("a  b"), QStringLiteral("$x\\"), QStringLiteral("\"") });
}

TEST_CASE("splitShellWords applies backslash escapes", "[splitShellWords]")
{
	CHECK(splitShellWords(QStringLiteral("a\\ b \\' \\\\")) == QStringList{ QStringLiteral("a b"), QStringLiteral("'"), QStringLiteral("\\") });
	CHECK(splitShellWords(QStringLiteral("a\\\nb")) == QStringList{ QStringLiteral("ab") });
	// Inside double quotes only $ ` " \ are escapable
	CHECK(splitShellWords(QStringLiteral("\"\\$ \\` \\\" \\\\ \\x '\"")) == QStringList{ QStringLiteral("$ ` \" \\ \\x '") });
}

TEST_CASE("splitShellWords joins adjacent pieces and keeps empty quoted words", "[splitShellWords]")
{
	CHECK(splitShellWords(QStringLiteral("a'b'\"c\"d")) == QStringList{ QStringLiteral("abcd") });
	CHECK(splitShellWords(QStringLiteral("'' \"\"")) == QStringList{ QString{}, QString{} });
}

TEST_CASE("splitShellWords fails on an unfinished quote or escape", "[splitShellWords]")
{
	CHECK(!splitShellWords(QStringLiteral("a 'b")));
	CHECK(!splitShellWords(QStringLiteral("a \"b")));
	CHECK(!splitShellWords(QStringLiteral("a\\")));
}

TEST_CASE("splitShellWords undoes shellQuotedPath", "[splitShellWords][shellQuotedPath]")
{
	const QString path = GENERATE(
		QStringLiteral("/plain/path"),
		QStringLiteral("/with space"),
		QStringLiteral("/it's"),
		QStringLiteral("/$HOME/*?[a]"),
		QStringLiteral("/tab\there"),
		QStringLiteral("/back\\slash"),
		QStringLiteral("/double\"quote"),
		QStringLiteral("/`cmd`;&|<>()")
	);

	CHECK(splitShellWords(shellQuotedPath(path)) == QStringList{ path });
	CHECK(splitShellWords(QStringLiteral("--dir=") % shellQuotedPath(path) % QStringLiteral(" next")) == QStringList{ QStringLiteral("--dir=") + path, QStringLiteral("next") });
}

TEST_CASE("deviceIdForPath resolves a path that does not exist yet through its nearest existing ancestor", "[deviceIdForPath]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());

	const auto existingDirDevice = deviceIdForPath(tempDir.path());
	REQUIRE(existingDirDevice.has_value());
	CHECK(deviceIdForPath(tempDir.path() + "/does_not_exist/child.txt") == existingDirDevice);
}
#endif

#ifdef _WIN32
TEST_CASE("resolveUserPath: a drive letter alone means that drive's root", "[resolveUserPath]")
{
	const QString base = QStringLiteral("C:/base/dir");
	CHECK(resolveUserPath(QStringLiteral("E:"), base) == QStringLiteral("E:/"));
	CHECK(resolveUserPath(QStringLiteral("e:"), base) == QStringLiteral("E:/"));
	CHECK(resolveUserPath(QStringLiteral("E:sub"), base) == QStringLiteral("E:/sub"));
	CHECK(resolveUserPath(QStringLiteral("E:\\x\\y"), base) == QStringLiteral("E:/x/y"));
}

TEST_CASE("resolveUserPath: relative and rooted text resolves against the base folder", "[resolveUserPath]")
{
	const QString base = QStringLiteral("C:/base/dir");
	CHECK(resolveUserPath(QStringLiteral("sub\\x"), base) == QStringLiteral("C:/base/dir/sub/x"));
	CHECK(resolveUserPath(QStringLiteral(".."), base) == QStringLiteral("C:/base"));
	CHECK(resolveUserPath(QStringLiteral("..\\..\\..\\.."), base) == QStringLiteral("C:/"));
	CHECK(resolveUserPath(QStringLiteral("\\rooted"), base) == QStringLiteral("C:/rooted"));

	const QString uncBase = QStringLiteral("//server/share/a");
	CHECK(resolveUserPath(QStringLiteral("/x"), uncBase) == QStringLiteral("//server/share/x"));
	CHECK(resolveUserPath(QStringLiteral(".."), uncBase) == QStringLiteral("//server/share"));
	CHECK(resolveUserPath(QStringLiteral("\\\\other\\share\\y"), base) == QStringLiteral("//other/share/y"));
}

TEST_CASE("resolveUserPath: nothing to resolve is empty", "[resolveUserPath]")
{
	CHECK(resolveUserPath({}, QStringLiteral("C:/")).isEmpty());
	CHECK(resolveUserPath(QStringLiteral("\\\\server"), QStringLiteral("C:/")).isEmpty()); // UNC without a share
}

TEST_CASE("resolveUserPath: environment variables expand, undefined ones stay as typed", "[resolveUserPath]")
{
	qputenv("FC_RESOLVE_TEST_DIR", "D:\\tmp");
	CHECK(resolveUserPath(QStringLiteral("%FC_RESOLVE_TEST_DIR%\\x"), QStringLiteral("C:/")) == QStringLiteral("D:/tmp/x"));
	qunsetenv("FC_RESOLVE_TEST_DIR");
	CHECK(resolveUserPath(QStringLiteral("%FC_RESOLVE_TEST_DIR%"), QStringLiteral("C:/base")) == QStringLiteral("C:/base/%FC_RESOLVE_TEST_DIR%"));
}
#else
TEST_CASE("resolveUserPath: relative text resolves against the base folder", "[resolveUserPath]")
{
	const QString base = QStringLiteral("/base/dir");
	CHECK(resolveUserPath(QStringLiteral("sub/x"), base) == QStringLiteral("/base/dir/sub/x"));
	CHECK(resolveUserPath(QStringLiteral(".."), base) == QStringLiteral("/base"));
	CHECK(resolveUserPath(QStringLiteral("../../../.."), base) == QStringLiteral("/"));
	CHECK(resolveUserPath(QStringLiteral("/abs/./x/../y/"), base) == QStringLiteral("/abs/y"));
	CHECK(resolveUserPath({}, base).isEmpty());
}

TEST_CASE("resolveUserPath: the home folder and environment variables expand", "[resolveUserPath]")
{
	const QString home = normalizedAbsolutePath(qEnvironmentVariable("HOME"));
	REQUIRE_FALSE(home.isEmpty());
	CHECK(resolveUserPath(QStringLiteral("~"), QStringLiteral("/")) == home);
	CHECK(resolveUserPath(QStringLiteral("~/x"), QStringLiteral("/")) == home % QStringLiteral("/x"));
	CHECK(resolveUserPath(QStringLiteral("~x"), QStringLiteral("/base")) == QStringLiteral("/base/~x"));

	qputenv("FC_RESOLVE_TEST_DIR", "/tmp/t");
	CHECK(resolveUserPath(QStringLiteral("$FC_RESOLVE_TEST_DIR/x"), QStringLiteral("/")) == QStringLiteral("/tmp/t/x"));
	CHECK(resolveUserPath(QStringLiteral("/my dir/${FC_RESOLVE_TEST_DIR}"), QStringLiteral("/")) == QStringLiteral("/my dir/tmp/t"));
	qunsetenv("FC_RESOLVE_TEST_DIR");
}

TEST_CASE("resolveUserPath: an undefined variable leaves the text as typed", "[resolveUserPath]")
{
	qunsetenv("FC_RESOLVE_TEST_UNDEFINED");
	CHECK(resolveUserPath(QStringLiteral("$FC_RESOLVE_TEST_UNDEFINED/x"), QStringLiteral("/base")) == QStringLiteral("/base/$FC_RESOLVE_TEST_UNDEFINED/x"));
	CHECK(resolveUserPath(QStringLiteral("$HOME/$FC_RESOLVE_TEST_UNDEFINED"), QStringLiteral("/base")) == QStringLiteral("/base/$HOME/$FC_RESOLVE_TEST_UNDEFINED"));
}

TEST_CASE("resolveUserPath: a command substitution is never run", "[resolveUserPath]")
{
	CHECK(resolveUserPath(QStringLiteral("$(echo /ran)"), QStringLiteral("/base")) == QStringLiteral("/base/$(echo /ran)"));
	CHECK(resolveUserPath(QStringLiteral("`echo /ran`$HOME"), QStringLiteral("/base")) == QStringLiteral("/base/`echo /ran`$HOME"));
}
#endif

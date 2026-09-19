#include "filesystemhelperfunctions.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


#define CATCH_CONFIG_MAIN
DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QStringBuilder>
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
#endif

#include "shell/cshell.h"
#include "shell/cshellcommand.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <functional>

namespace {

struct CommandResult
{
	QString output;
	int exitCode = -1;
	bool normalExit = false;
	bool finished = false; // False when the command outlived the wait
};

// Runs `command` until it ends, for 30 seconds at most. `afterStart` gets the running command.
CommandResult runCommand(const QString& command, const QString& workingDir, const std::function<void(CShellCommand&)>& afterStart = {})
{
	CommandResult result;
	QEventLoop loop;
	CShellCommand shellCommand{ command, workingDir };
	shellCommand.onOutput = [&](const QString& text) { result.output += text; };
	shellCommand.onFinished = [&](const int exitCode, const bool normalExit) {
		result.exitCode = exitCode;
		result.normalExit = normalExit;
		result.finished = true;
		loop.quit();
	};

	REQUIRE(shellCommand.start().has_value());
	if (afterStart)
		afterStart(shellCommand);

	QTimer::singleShot(30'000, &loop, &QEventLoop::quit);
	loop.exec();
	return result;
}

QString nativeTempPath()
{
	return toNativeSeparators(QDir::tempPath());
}

void writeFile(const QString& path, const QByteArray& content)
{
	QFile file{ path };
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(content) == content.size());
}

} // namespace

TEST_CASE("A command's output and exit code are reported", "[shellcommand]")
{
	const CommandResult result = runCommand(QStringLiteral("echo hello"), nativeTempPath());
	REQUIRE(result.finished);
	CHECK(result.normalExit);
	CHECK(result.exitCode == 0);
	CHECK(result.output.trimmed() == QStringLiteral("hello"));
}

TEST_CASE("A failing command reports its exit code", "[shellcommand]")
{
	const CommandResult result = runCommand(QStringLiteral("exit 3"), nativeTempPath());
	REQUIRE(result.finished);
	CHECK(result.normalExit);
	CHECK(result.exitCode == 3);
}

TEST_CASE("A command runs in its working folder", "[shellcommand]")
{
	QTemporaryDir dir;
	REQUIRE(dir.isValid());
	writeFile(dir.filePath(QStringLiteral("marker.txt")), "inside");

#ifdef _WIN32
	const QString printMarker = QStringLiteral("type marker.txt");
#else
	const QString printMarker = QStringLiteral("cat marker.txt");
#endif
	const CommandResult result = runCommand(printMarker, toNativeSeparators(dir.path()));
	REQUIRE(result.finished);
	CHECK(result.output.trimmed() == QStringLiteral("inside"));
}

TEST_CASE("Escape sequences are removed from the output", "[shellcommand]")
{
	// A window title, then coloured text
#ifdef _WIN32
	const QChar escape{ 0x1B };
	const QChar bell{ 0x07 };
	const QString printColoured = QStringLiteral("echo ") % escape % QStringLiteral("]0;title") % bell % escape % QStringLiteral("[31mred") % escape % QStringLiteral("[0m plain");
#else
	const QString printColoured = QStringLiteral("printf '\\033]0;title\\007\\033[31mred\\033[0m plain\\n'");
#endif
	const CommandResult result = runCommand(printColoured, nativeTempPath());
	REQUIRE(result.finished);
	CHECK(result.output.trimmed() == QStringLiteral("red plain"));
}

TEST_CASE("terminateTree ends a running command", "[shellcommand]")
{
	// Longer than runCommand waits: only termination finishes it
#ifdef _WIN32
	const QString longRunning = QStringLiteral("ping -n 60 127.0.0.1");
#else
	const QString longRunning = QStringLiteral("sleep 60");
#endif
	const CommandResult result = runCommand(longRunning, nativeTempPath(), [](CShellCommand& command) { command.terminateTree(); });
	CHECK(result.finished);
}

#ifdef _WIN32

TEST_CASE("A command of maxCommandLength runs", "[shellcommand]")
{
	const QString dir = nativeTempPath();
	const QString echo = QStringLiteral("echo ");
	const qsizetype textLength = CShellCommand::maxCommandLength(dir) - echo.size();

	const CommandResult result = runCommand(echo % QString(textLength, QChar{ u'a' }), dir);
	REQUIRE(result.finished);
	CHECK(result.exitCode == 0);
	CHECK(result.output.trimmed().size() == textLength);
}

namespace {

QString programName(const OsShell::ProgramInvocation& invocation)
{
	return QFileInfo{ invocation.programPath }.fileName().toLower();
}

} // namespace

TEST_CASE("A line with shell syntax needs the shell", "[launching]")
{
	const QString dir = nativeTempPath();
	for (const QString& line : {
		QStringLiteral("explorer | more"),
		QStringLiteral("explorer && explorer"),
		QStringLiteral("explorer > out.txt"),
		QStringLiteral("(explorer)"),
		QStringLiteral("explorer ^x"),
		QStringLiteral("explorer %TEMP%"),
		QStringLiteral("explorer \"%TEMP%\""), // cmd expands a variable inside quotes too
	})
	{
		CAPTURE(line);
		CHECK(!OsShell::guiProgramInvocation(line, dir));
		CHECK(!OsShell::directInvocation(line, dir));
	}
}

TEST_CASE("A GUI program launches directly, with its arguments as typed", "[launching]")
{
	const QString dir = nativeTempPath();
	// Operators inside quotes are part of an argument
	const QString line = QStringLiteral("  explorer /select \"a & b | c\"  ");

	for (const auto& invocation : { OsShell::guiProgramInvocation(line, dir), OsShell::directInvocation(line, dir) })
	{
		REQUIRE(invocation);
		CHECK(programName(*invocation) == QStringLiteral("explorer.exe"));
		CHECK(invocation->arguments == QStringLiteral("/select \"a & b | c\""));
		CHECK(invocation->workingDir == dir);
	}
}

TEST_CASE("A console program launches directly only through directInvocation", "[launching]")
{
	const QString dir = nativeTempPath();
	const QString line = QStringLiteral("ping -n 1 127.0.0.1");

	CHECK(!OsShell::guiProgramInvocation(line, dir));

	const auto invocation = OsShell::directInvocation(line, dir);
	REQUIRE(invocation);
	CHECK(programName(*invocation) == QStringLiteral("ping.exe"));
	CHECK(invocation->arguments == QStringLiteral("-n 1 127.0.0.1"));
}

TEST_CASE("A program that is not found needs the shell", "[launching]")
{
	const QString dir = nativeTempPath();
	for (const QString& line : { QStringLiteral("no-such-program-xyz arg"), QStringLiteral("\"explorer") })
	{
		CAPTURE(line);
		CHECK(!OsShell::guiProgramInvocation(line, dir));
		CHECK(!OsShell::directInvocation(line, dir));
	}

	CHECK(OsShell::commandLineProgramPath(QStringLiteral("no-such-program-xyz arg"), dir).isEmpty());
}

TEST_CASE("A bare cmd launches directly; cmd with arguments is a console program", "[launching]")
{
	const QString dir = nativeTempPath();

	const auto bare = OsShell::guiProgramInvocation(QStringLiteral("cmd"), dir);
	REQUIRE(bare);
	CHECK(programName(*bare) == QStringLiteral("cmd.exe"));
	CHECK(bare->arguments.isEmpty());
	CHECK(bare->workingDir == dir);

	CHECK(!OsShell::guiProgramInvocation(QStringLiteral("cmd /c echo x"), dir));
	CHECK(OsShell::directInvocation(QStringLiteral("cmd /c echo x"), dir));
}

TEST_CASE("A bare cmd reaches a UNC folder through pushd", "[launching]")
{
	const QString uncDir = QStringLiteral("\\\\localhost\\C$\\Windows");

	const auto invocation = OsShell::guiProgramInvocation(QStringLiteral("cmd"), uncDir);
	REQUIRE(invocation);
	CHECK(programName(*invocation) == QStringLiteral("cmd.exe"));
	CHECK(invocation->arguments == QStringLiteral("/k pushd \"\\\\localhost\\C$\\Windows\""));
	CHECK(invocation->workingDir.isEmpty());
}

TEST_CASE("A batch file needs the shell; any other file launches directly", "[launching]")
{
	QTemporaryDir temporaryDir;
	REQUIRE(temporaryDir.isValid());
	const QString dir = toNativeSeparators(temporaryDir.path());
	writeFile(temporaryDir.filePath(QStringLiteral("tool.bat")), "@echo off\r\n");
	writeFile(temporaryDir.filePath(QStringLiteral("notes.txt")), "notes");

	// The variable takes the working folder out of the search, and the tests' environment may have it set
	static constexpr const char* noCurrentDirectoryVariable = "NoDefaultCurrentDirectoryInExePath";
	const QByteArray inheritedValue = qgetenv(noCurrentDirectoryVariable);
	EXEC_ON_SCOPE_EXIT([&] {
		if (inheritedValue.isNull())
			qunsetenv(noCurrentDirectoryVariable);
		else
			qputenv(noCurrentDirectoryVariable, inheritedValue);
	});
	qunsetenv(noCurrentDirectoryVariable);

	SECTION("NoDefaultCurrentDirectoryInExePath excludes the working folder")
	{
		qputenv(noCurrentDirectoryVariable, "1");
		CHECK(OsShell::commandLineProgramPath(QStringLiteral("tool.bat arg"), dir).isEmpty());
		CHECK(!OsShell::directInvocation(QStringLiteral("notes.txt"), dir));
	}

	SECTION("A batch file, named with and without its extension")
	{
		for (const QString& line : { QStringLiteral("tool.bat arg"), QStringLiteral("tool arg") })
		{
			CAPTURE(line);
			CHECK(QFileInfo{ OsShell::commandLineProgramPath(line, dir) }.fileName().toLower() == QStringLiteral("tool.bat"));
			CHECK(!OsShell::guiProgramInvocation(line, dir));
			CHECK(!OsShell::directInvocation(line, dir));
		}
	}

	SECTION("A document in the working folder")
	{
		const QString line = QStringLiteral("notes.txt");
		CHECK(!OsShell::guiProgramInvocation(line, dir));

		const auto invocation = OsShell::directInvocation(line, dir);
		REQUIRE(invocation);
		CHECK(programName(*invocation) == QStringLiteral("notes.txt"));
		CHECK(invocation->arguments.isEmpty());
	}

	SECTION("A quoted path with spaces")
	{
		REQUIRE(QDir{ temporaryDir.path() }.mkdir(QStringLiteral("my folder")));
		const QString document = temporaryDir.filePath(QStringLiteral("my folder/my notes.txt"));
		writeFile(document, "notes");

		const auto invocation = OsShell::directInvocation('"' % toNativeSeparators(document) % QStringLiteral("\" arg"), nativeTempPath());
		REQUIRE(invocation);
		CHECK(QFileInfo{ invocation->programPath } == QFileInfo{ document });
		CHECK(invocation->arguments == QStringLiteral("arg"));
	}
}

#else

TEST_CASE("A detached launch fails when sh cannot find the leading command", "[launching]")
{
	const QString dir = nativeTempPath();

	CHECK(OsShell::runCommandLineDetached(QStringLiteral("true"), dir).has_value());
	CHECK(!OsShell::runCommandLineDetached(QStringLiteral("no-such-program-xyz --flag"), dir).has_value());
	// A leading assignment is not a word sh takes literally: the line launches unchecked
	CHECK(OsShell::runCommandLineDetached(QStringLiteral("FOO=1 no-such-program-xyz 2>/dev/null"), dir).has_value());
}

TEST_CASE("commandLineProgramPath finds a program in PATH", "[launching]")
{
	const QString dir = nativeTempPath();

	CHECK(!OsShell::commandLineProgramPath(QStringLiteral("sh -c true"), dir).isEmpty());
	CHECK(OsShell::commandLineProgramPath(QStringLiteral("no-such-program-xyz arg"), dir).isEmpty());
}

#endif

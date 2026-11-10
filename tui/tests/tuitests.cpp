#include "cterminalsession.h"

#include "qtcore_helpers/catch_qt.hpp"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

using namespace std::chrono_literals;
using namespace std::string_view_literals;
namespace Keys = TerminalKeys;

namespace {

constexpr int screenRows = 30, screenColumns = 100;
// The layout at this size: the menu bar, the two panel windows, the hint row and the key row
constexpr int panelStatusRow = screenRows - 4, hintRow = screenRows - 2, keyRow = screenRows - 1;
constexpr int panelWidth = screenColumns / 2;

enum class Side { Left, Right };

// A panel's columns of a screen row, inside its frame
[[nodiscard]] QString panelPart(const QString& row, Side side)
{
	return row.mid(side == Side::Left ? 1 : panelWidth + 1, panelWidth - 2);
}

[[nodiscard]] QString panelStatus(const CTerminalScreen& screen, Side side = Side::Left)
{
	return panelPart(screen.row(panelStatusRow), side).trimmed();
}

// The status line starts with the current item's name, then two spaces
[[nodiscard]] bool cursorIsOn(const CTerminalScreen& screen, const QString& name, Side side = Side::Left)
{
	return panelStatus(screen, side).startsWith(name + QStringLiteral("  "));
}

[[nodiscard]] bool panelLists(const CTerminalScreen& screen, const QString& name, Side side = Side::Left)
{
	for (int row = 2; row < panelStatusRow; ++row)
	{
		if (panelPart(screen.row(row), side).trimmed().startsWith(name + u' ') || panelPart(screen.row(row), side).trimmed() == name)
			return true;
	}
	return false;
}

[[nodiscard]] QString quoted(const QString& text)
{
	return u'"' + text + u'"';
}

// A temporary folder holding the files a test works on, the settings and, on Unix, the home folder
class Sandbox
{
public:
	Sandbox()
	{
		REQUIRE(_root.isValid());
		for (const char* folder : { "home", "settings", "work", "other" })
			REQUIRE(QDir{ _root.path() }.mkpath(QString::fromLatin1(folder)));
	}

	[[nodiscard]] QString path(const QString& relativePath) const { return _root.filePath(relativePath); }
	[[nodiscard]] QString work(const QString& name) const { return path(QStringLiteral("work/") + name); }

	void createFile(const QString& relativePath, const QByteArray& content = "content") const
	{
		QFile file{ path(relativePath) };
		REQUIRE(file.open(QFile::WriteOnly));
		REQUIRE(file.write(content) == content.size());
	}

#ifndef _WIN32
	// FAT and exFAT refuse the permission change but mount every file executable
	void createScript(const QString& relativePath, const QByteArray& content) const
	{
		createFile(relativePath, content);
		QFile::setPermissions(path(relativePath), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
		REQUIRE(QFileInfo{ path(relativePath) }.isExecutable());
	}
#endif

private:
	QTemporaryDir _root{ QDir::tempPath() + QStringLiteral("/tui_test-XXXXXX") };
};

[[nodiscard]] QString testExecutable()
{
	return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

// The command for a pager or editor variable: this executable's stub mode, recording into recordPath
[[nodiscard]] QString recordingCommand(const QString& recordPath)
{
	return quoted(testExecutable()) + QStringLiteral(" --record ") + quoted(QDir::toNativeSeparators(recordPath));
}

void requireScreen(CTerminalSession& session, const std::function<bool(const CTerminalScreen&)>& condition, const QString& expectation)
{
	if (session.waitFor(condition))
		return;

	const std::optional<int> exitCode = session.waitForExit(0ms);
	FAIL("Timed out waiting for " << expectation.toStdString() << (exitCode ? ", the program exited with code " + std::to_string(*exitCode) : std::string{})
		<< "\n" << session.screen().text().toStdString());
}

void requireText(CTerminalSession& session, const QString& text)
{
	requireScreen(session, [&text](const CTerminalScreen& screen) { return screen.contains(text); }, quoted(text));
}

// Types one character at a time, each once the previous one shows.
// Turbo Vision takes a burst of text keys for a paste (TEventQueue::getKeyOrPasteEvent), and an Enter sent right behind one is lost.
void typeText(CTerminalSession& session, const QString& text)
{
	for (const QChar c : text)
	{
		const QString screenBefore = session.screen().text();
		session.send(QString{ c });
		requireScreen(session, [&screenBefore](const CTerminalScreen& screen) { return screen.text() != screenBefore; }, QStringLiteral("\"%1\" to show").arg(c));
	}
}

// fcterm with its settings in the sandbox and no pager, editor or desktop of the user's
[[nodiscard]] std::unique_ptr<CTerminalSession> startFcterm(const Sandbox& sandbox, const std::map<QString, QString>& variables = {})
{
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	for (const char* variable : { "VISUAL", "EDITOR", "PAGER", "DISPLAY", "WAYLAND_DISPLAY", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME" })
		environment.remove(QString::fromLatin1(variable));
	environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
#ifndef _WIN32
	environment.insert(QStringLiteral("HOME"), sandbox.path(QStringLiteral("home")));
#ifdef __APPLE__
	environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("en_US.UTF-8"));
#else
	environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
#endif
#endif
	for (const auto& [name, value] : variables)
		environment.insert(name, value);

	CTerminalSession::Program program;
	program.path = QCoreApplication::applicationDirPath() + QStringLiteral("/fcterm");
#ifdef _WIN32
	program.path += QStringLiteral(".exe");
#endif
	program.arguments = { QStringLiteral("--settings-dir"), QDir::toNativeSeparators(sandbox.path(QStringLiteral("settings"))) };
	program.workingDir = sandbox.path({});
	program.environment = environment;

	auto session = std::make_unique<CTerminalSession>(program, screenRows, screenColumns);
	REQUIRE(session->startError().isEmpty());
	requireText(*session, QStringLiteral("F2 Rename"));
	return session;
}

// Shows `folder` in the focused panel. `entry` is a name in it to wait for.
void goTo(CTerminalSession& session, const QString& folder, const QString& entry, Side side = Side::Left)
{
	session.send(Keys::AltD);
	requireText(session, QStringLiteral("Folder:"));
	typeText(session, QDir::toNativeSeparators(folder));
	session.send(Keys::Enter);
	requireScreen(session, [&](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Folder:")) && panelLists(screen, entry, side); },
		QStringLiteral("%1 listed in %2").arg(entry, folder));
}

void moveCursorTo(CTerminalSession& session, const QString& name)
{
	session.send(Keys::Home);
	requireScreen(session, [](const CTerminalScreen& screen) { return cursorIsOn(screen, QStringLiteral("..")); }, QStringLiteral("the cursor on .."));
	while (!cursorIsOn(session.screen(), name))
	{
		const QString statusBefore = panelStatus(session.screen());
		session.send(Keys::Down);
		requireScreen(session, [&](const CTerminalScreen& screen) { return panelStatus(screen) != statusBefore; },
			QStringLiteral("the cursor to move on its way to %1").arg(name));
	}
}

[[nodiscard]] QString canonical(const QString& path)
{
	return QFileInfo{ path }.canonicalFilePath();
}

// The stub's record: its working folder, then its arguments
[[nodiscard]] QStringList recordedLines(const QString& recordPath)
{
	QFile record{ recordPath };
	REQUIRE(record.open(QFile::ReadOnly));
	return QString::fromUtf8(record.readAll()).split(u'\n', Qt::SkipEmptyParts);
}

// Opens `file` in the sandbox's work folder with `key`, through the stub set as `variable`, and returns the stub's record
[[nodiscard]] QStringList recordedLaunch(const Sandbox& sandbox, const char* variable, const QString& file, std::string_view key)
{
	const QString recordPath = sandbox.path(QStringLiteral("record.txt"));
	auto session = startFcterm(sandbox, { { QString::fromLatin1(variable), recordingCommand(recordPath) } });
	goTo(*session, sandbox.work({}), file);
	moveCursorTo(*session, file);
	session->send(key);
	requireScreen(*session, [&](const CTerminalScreen&) { return QFile::exists(recordPath); }, QStringLiteral("the stub's record"));
	return recordedLines(recordPath);
}

void requireRecordedLaunch(const QStringList& record, const QString& expectedFile)
{
	REQUIRE(record.size() == 2);
	CHECK(canonical(record[0].mid(4)) == canonical(QFileInfo{ expectedFile }.path()));
	CHECK(canonical(record[1].mid(4)) == canonical(expectedFile));
}

} // namespace

TEST_CASE("The status rows show the keys", "[tui]")
{
	const Sandbox sandbox;
	auto session = startFcterm(sandbox);
	CHECK(session->screen().row(hintRow).contains(QStringLiteral("Tab Panel")));
	CHECK(session->screen().row(hintRow).contains(QStringLiteral("Alt-X Exit")));
	CHECK(session->screen().row(keyRow).contains(QStringLiteral("F2 Rename")));
	CHECK(session->screen().row(keyRow).contains(QStringLiteral("F8 Delete")));
}

TEST_CASE("Alt+X exits with code 0", "[tui]")
{
	const Sandbox sandbox;
	auto session = startFcterm(sandbox);
	session->send(Keys::AltX);
	CHECK(session->waitForExit() == 0);
}

TEST_CASE("F7 creates a folder and puts the cursor on it", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/existing.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("existing.txt"));

	session->send(Keys::F7);
	requireText(*session, QStringLiteral("Name of the new folder"));
	typeText(*session, QStringLiteral("created"));
	session->send(Keys::Enter);
	requireScreen(*session, [](const CTerminalScreen& screen) { return cursorIsOn(screen, QStringLiteral("created")); }, QStringLiteral("the cursor on the new folder"));
	CHECK(QFileInfo{ sandbox.work(QStringLiteral("created")) }.isDir());
}

TEST_CASE("Shift+F7 creates a file and puts the cursor on it", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/existing.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("existing.txt"));

	session->send(Keys::ShiftF7);
	requireText(*session, QStringLiteral("Name of the new file"));
	typeText(*session, QStringLiteral("created.txt"));
	session->send(Keys::Enter);
	requireScreen(*session, [](const CTerminalScreen& screen) { return cursorIsOn(screen, QStringLiteral("created.txt")); }, QStringLiteral("the cursor on the new file"));
	CHECK(QFileInfo{ sandbox.work(QStringLiteral("created.txt")) }.isFile());
}

TEST_CASE("F2 onto an existing name asks, then replaces it", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"), "first");
	sandbox.createFile(QStringLiteral("work/b.txt"), "second");
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("a.txt"));
	moveCursorTo(*session, QStringLiteral("a.txt"));

	session->send(Keys::F2);
	requireText(*session, QStringLiteral("New name for"));
	typeText(*session, QStringLiteral("b.txt"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("already exists"));
	session->send(Keys::Enter);
	requireScreen(*session, [](const CTerminalScreen& screen) { return cursorIsOn(screen, QStringLiteral("b.txt")) && !panelLists(screen, QStringLiteral("a.txt")); },
		QStringLiteral("b.txt alone, under the cursor"));

	QFile replaced{ sandbox.work(QStringLiteral("b.txt")) };
	REQUIRE(replaced.open(QFile::ReadOnly));
	CHECK(replaced.readAll() == "first");
}

TEST_CASE("Shift+F8 deletes permanently after confirmation", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/keep.txt"));
	sandbox.createFile(QStringLiteral("work/remove.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("remove.txt"));
	moveCursorTo(*session, QStringLiteral("remove.txt"));

	session->send(Keys::ShiftF8);
	requireText(*session, QStringLiteral("permanently?"));
	session->send(Keys::Enter);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !panelLists(screen, QStringLiteral("remove.txt")); }, QStringLiteral("remove.txt gone from the panel"));
	CHECK_FALSE(QFile::exists(sandbox.work(QStringLiteral("remove.txt"))));
	CHECK(QFile::exists(sandbox.work(QStringLiteral("keep.txt"))));
}

TEST_CASE("F5 copies into the other panel's folder", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/copied.txt"), "copied content");
	sandbox.createFile(QStringLiteral("other/resident.txt"));
	auto session = startFcterm(sandbox);
	session->send(Keys::Tab);
	goTo(*session, sandbox.path(QStringLiteral("other")), QStringLiteral("resident.txt"), Side::Right);
	session->send(Keys::Tab);
	goTo(*session, sandbox.work({}), QStringLiteral("copied.txt"));
	moveCursorTo(*session, QStringLiteral("copied.txt"));

	session->send(Keys::F5);
	requireText(*session, QStringLiteral("to:"));
	session->send(Keys::Enter);
	const QString copy = sandbox.path(QStringLiteral("other/copied.txt"));
	requireScreen(*session, [&](const CTerminalScreen& screen) { return QFileInfo{ copy }.size() == 14 && panelLists(screen, QStringLiteral("copied.txt"), Side::Right); },
		QStringLiteral("the copy in the right panel"));
}

TEST_CASE("Ctrl+S hides and shows hidden files", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/visible.txt"));
#ifdef _WIN32
	const QString hiddenName = QStringLiteral("hidden.txt");
	sandbox.createFile(QStringLiteral("work/") + hiddenName);
	REQUIRE(SetFileAttributesW((const wchar_t*)QDir::toNativeSeparators(sandbox.work(hiddenName)).utf16(), FILE_ATTRIBUTE_HIDDEN));
#else
	const QString hiddenName = QStringLiteral(".hidden.txt");
	sandbox.createFile(QStringLiteral("work/") + hiddenName);
#endif
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), hiddenName);

	session->send(Keys::CtrlS);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return !panelLists(screen, hiddenName) && panelLists(screen, QStringLiteral("visible.txt")); },
		QStringLiteral("the hidden file gone"));
	session->send(Keys::CtrlS);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return panelLists(screen, hiddenName); }, QStringLiteral("the hidden file back"));
}

TEST_CASE("+ and - select and deselect by pattern", "[tui]")
{
	const Sandbox sandbox;
	for (const char* name : { "a.txt", "b.txt", "c.doc" })
		sandbox.createFile(QStringLiteral("work/") + QString::fromLatin1(name));
	REQUIRE(QDir{ sandbox.work({}) }.mkdir(QStringLiteral("sub")));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("c.doc"));

	const auto requireStatus = [&](const QString& status) {
		requireScreen(*session, [&](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(status); }, quoted(status));
	};
	// The input line starts with "*", all of it selected, so the typed pattern replaces it
	const auto applyPattern = [&](std::string_view key, const QString& pattern) {
		session->send(key);
		requireText(*session, QStringLiteral("Names matching"));
		typeText(*session, pattern);
		session->send(Keys::Enter);
	};

	applyPattern("+", QStringLiteral("?.txt"));
	requireStatus(QStringLiteral("2 files, 0 folders selected"));
	applyPattern("-", QStringLiteral("b*"));
	requireStatus(QStringLiteral("1 files, 0 folders selected"));

	session->send("+"sv);
	requireText(*session, QStringLiteral("Files only"));
	session->send(Keys::AltF);
	requireText(*session, QStringLiteral("[X] Files only"));
	session->send(Keys::Enter);
	requireStatus(QStringLiteral("3 files, 0 folders selected"));

	session->send("+"sv);
	requireText(*session, QStringLiteral("Names matching"));
	session->send(Keys::Enter);
	requireStatus(QStringLiteral("3 files, 1 folders selected"));
}

TEST_CASE("= selects the files with the current file's extension", "[tui]")
{
	const Sandbox sandbox;
	for (const char* name : { "a.txt", "b.txt", "c.doc" })
		sandbox.createFile(QStringLiteral("work/") + QString::fromLatin1(name));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("c.doc"));
	moveCursorTo(*session, QStringLiteral("b.txt"));

	session->send("="sv);
	requireScreen(*session, [](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(QStringLiteral("2 files, 0 folders selected")); },
		QStringLiteral("the two .txt files selected"));
}

TEST_CASE("F3 opens the pager on the file, in its folder", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/viewed.txt"));
	requireRecordedLaunch(recordedLaunch(sandbox, "PAGER", QStringLiteral("viewed.txt"), Keys::F3), sandbox.work(QStringLiteral("viewed.txt")));
}

TEST_CASE("F4 opens the editor on the file, in its folder", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/edited.txt"));
	requireRecordedLaunch(recordedLaunch(sandbox, "EDITOR", QStringLiteral("edited.txt"), Keys::F4), sandbox.work(QStringLiteral("edited.txt")));
}

TEST_CASE("F4 with a missing editor says why it failed", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/edited.txt"));
	auto session = startFcterm(sandbox, { { QStringLiteral("EDITOR"), QStringLiteral("no-such-editor-for-tui-test") } });
	goTo(*session, sandbox.work({}), QStringLiteral("edited.txt"));
	moveCursorTo(*session, QStringLiteral("edited.txt"));

	session->send(Keys::F4);
	requireText(*session, QStringLiteral("Failed to start no-such-editor-for-tui-test"));
}

TEST_CASE("Enter runs a program and keeps its output until Enter", "[tui]")
{
	const Sandbox sandbox;
#ifdef _WIN32
	const QString program = QStringLiteral("program.bat");
	sandbox.createFile(QStringLiteral("work/") + program, "@echo output of the program\r\n");
#else
	const QString program = QStringLiteral("program.sh");
	sandbox.createScript(QStringLiteral("work/") + program, "#!/bin/sh\necho output of the program\n");
#endif
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), program);
	moveCursorTo(*session, program);

	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("Press Enter to return to File Commander"));
	CHECK(session->screen().contains(QStringLiteral("output of the program")));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("F2 Rename"));
}

#ifndef _WIN32
TEST_CASE("Ctrl+C stops the running program but not fcterm", "[tui]")
{
	const Sandbox sandbox;
	// Not "echo; sleep": an interrupt that reaches the shell before it starts sleep leaves sleep running
	sandbox.createScript(QStringLiteral("work/slow.sh"), "#!/bin/sh\nexec " + quoted(testExecutable()).toUtf8() + " --print-then-sleep started\n");
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("slow.sh"));
	moveCursorTo(*session, QStringLiteral("slow.sh"));

	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("started"));
	session->send(Keys::CtrlC);
	requireText(*session, QStringLiteral("Press Enter to return to File Commander"));
	CHECK_FALSE(session->screen().contains(QStringLiteral("finished")));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("F2 Rename"));
}

TEST_CASE("Enter on a document without a desktop opens the pager", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/document.md"));
	requireRecordedLaunch(recordedLaunch(sandbox, "PAGER", QStringLiteral("document.md"), Keys::Enter), sandbox.work(QStringLiteral("document.md")));
}
#endif

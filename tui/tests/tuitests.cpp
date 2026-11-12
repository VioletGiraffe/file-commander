#include "cterminalsession.h"

#include "qtcore_helpers/catch_qt.hpp"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using namespace std::chrono_literals;
using namespace std::string_view_literals;
namespace Keys = TerminalKeys;

namespace {

constexpr int screenRows = 30, screenColumns = 100;
// The layout at this size: the menu bar, the two panel windows, the hint row and the key row
constexpr int panelStatusRow = screenRows - 4, hintRow = screenRows - 2, keyRow = screenRows - 1;
constexpr int tabStripRow = 2; // While a side has more than one tab
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

// The status line is the current item's name, followed by two spaces and its details except on [..]
[[nodiscard]] bool cursorIsOn(const CTerminalScreen& screen, const QString& name, Side side = Side::Left)
{
	const QString status = panelStatus(screen, side);
	return status == name || status.startsWith(name + QStringLiteral("  "));
}

// True when rowPattern matches one of the panel's list rows, with the row's padding trimmed
[[nodiscard]] bool panelHasRowMatching(const CTerminalScreen& screen, const QRegularExpression& rowPattern, Side side = Side::Left)
{
	for (int row = 2; row < panelStatusRow; ++row)
	{
		if (rowPattern.match(panelPart(screen.row(row), side).trimmed()).hasMatch())
			return true;
	}
	return false;
}

// A row shows a name whole, or with its extension in the Ext column
[[nodiscard]] bool panelLists(const CTerminalScreen& screen, const QString& name, Side side = Side::Left)
{
	const qsizetype dot = name.lastIndexOf(u'.');
	QString pattern = QRegularExpression::escape(name);
	if (dot > 0 && dot < name.size() - 1)
		pattern += u'|' + QRegularExpression::escape(name.left(dot)) + QStringLiteral(" +") + QRegularExpression::escape(name.mid(dot + 1));
	return panelHasRowMatching(screen, QRegularExpression{ QStringLiteral("^(?:%1)(?: |$)").arg(pattern) }, side);
}

// The left panel's column header row: the one under the panel's top edge, or under its tab strip
[[nodiscard]] std::optional<int> columnHeaderRow(const CTerminalScreen& screen, int leftPanelWidth = panelWidth)
{
	for (const int row : { 2, 3 })
	{
		if (screen.row(row).mid(1, leftPanelWidth - 2).trimmed().startsWith(QStringLiteral("Name")))
			return row;
	}
	return std::nullopt;
}

// A left click on a screen cell, as xterm's SGR mouse mode sends it: it counts from 1
[[nodiscard]] QString leftClick(int column, int row)
{
	return QStringLiteral("\x1b[<0;%1;%2M\x1b[<0;%1;%2m").arg(column + 1).arg(row + 1);
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

// Types one character at a time, each once the previous one shows: as a change of the text, or of the cursor for a space at the end of a line.
// Turbo Vision takes a burst of text keys for a paste (TEventQueue::getKeyOrPasteEvent), and an Enter sent right behind one is lost.
void typeText(CTerminalSession& session, const QString& text)
{
	for (const QChar c : text)
	{
		const CTerminalScreen& screen = session.screen();
		const QString screenBefore = screen.text();
		const int rowBefore = screen.cursorRow(), columnBefore = screen.cursorColumn();
		session.send(QString{ c });
		requireScreen(session, [&](const CTerminalScreen& now) { return now.text() != screenBefore || now.cursorRow() != rowBefore || now.cursorColumn() != columnBefore; },
			QStringLiteral("\"%1\" to show").arg(c));
	}
}

struct FctermStart
{
	std::map<QString, QString> variables; // More of fcterm's environment
	QString workingFolder; // The sandbox root when empty
	QStringList extraArguments;
	int columns = screenColumns;
};

// fcterm with its settings in the sandbox and no pager, editor or desktop of the user's
[[nodiscard]] std::unique_ptr<CTerminalSession> startFcterm(const Sandbox& sandbox, const FctermStart& start = {})
{
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	for (const char* variable : { "VISUAL", "EDITOR", "PAGER", "DISPLAY", "WAYLAND_DISPLAY", "SSH_CONNECTION", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME" })
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
	for (const auto& [name, value] : start.variables)
		environment.insert(name, value);

	CTerminalSession::Program program;
	program.path = QCoreApplication::applicationDirPath() + QStringLiteral("/fcterm");
#ifdef _WIN32
	program.path += QStringLiteral(".exe");
#endif
	program.arguments = QStringList{ QStringLiteral("--settings-dir"), QDir::toNativeSeparators(sandbox.path(QStringLiteral("settings"))) } + start.extraArguments;
	program.workingDir = start.workingFolder.isEmpty() ? sandbox.path({}) : start.workingFolder;
	program.environment = environment;

	auto session = std::make_unique<CTerminalSession>(program, screenRows, start.columns);
	REQUIRE(session->startError().isEmpty());
	requireText(*session, QStringLiteral("F2 Rename"));
	return session;
}

// The first screen row holding the text; -1 for none
[[nodiscard]] int rowWith(const CTerminalScreen& screen, const QString& text)
{
	for (int row = 0; row < screenRows; ++row)
	{
		if (screen.row(row).contains(text))
			return row;
	}
	return -1;
}

// Waits for a text prompt (textPrompt() in the app) whose label holds `label`, with the cursor in its input line.
// A frame can arrive in several reads, and Turbo Vision places the cursor last: text typed before then can be read as a paste.
// The input line is two rows under the label: the label must fit one row.
void requirePromptInput(CTerminalSession& session, const QString& label)
{
	requireScreen(session, [&label](const CTerminalScreen& screen) {
		const int labelRow = rowWith(screen, label);
		return labelRow >= 0 && screen.cursorRow() == labelRow + 2;
	}, quoted(label) + QStringLiteral(" with the cursor in its input line"));
}

// Shows `folder` in the focused panel. `entry` is a name in it to wait for.
void goTo(CTerminalSession& session, const QString& folder, const QString& entry, Side side = Side::Left)
{
	session.send(Keys::AltD);
	requirePromptInput(session, QStringLiteral("Folder:"));
	typeText(session, QDir::toNativeSeparators(folder));
	session.send(Keys::Enter);
	requireScreen(session, [&](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Folder:")) && panelLists(screen, entry, side); },
		QStringLiteral("%1 listed in %2").arg(entry, folder));
}

// Opens the search dialog and returns the name field's row, once the cursor is in that field: see requirePromptInput()
int openFindDialog(CTerminalSession& session)
{
	session.send(Keys::AltF7);
	int nameRow = -1;
	requireScreen(session, [&nameRow](const CTerminalScreen& screen) {
		nameRow = rowWith(screen, QStringLiteral("Search for:"));
		return nameRow >= 0 && screen.cursorRow() == nameRow;
	}, QStringLiteral("the search dialog, with the cursor in the name field"));
	return nameRow;
}

// Opens the search dialog, searches for the name and waits for the status to show
void findByName(CTerminalSession& session, const QString& name, const QString& status)
{
	openFindDialog(session);
	typeText(session, name); // The name field's text starts selected, so this replaces it
	session.send(Keys::Enter);
	requireText(session, status);
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

// Opens `file` in the sandbox's work folder with `key`, through the stub set as `variable`, and returns the stub's record.
// otherVariables: more of fcterm's environment
[[nodiscard]] QStringList recordedLaunch(const Sandbox& sandbox, const char* variable, const QString& file, std::string_view key, std::map<QString, QString> otherVariables = {})
{
	const QString recordPath = sandbox.path(QStringLiteral("record.txt"));
	otherVariables[QString::fromLatin1(variable)] = recordingCommand(recordPath);
	auto session = startFcterm(sandbox, { .variables = otherVariables });
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
	requirePromptInput(*session, QStringLiteral("Name of the new folder"));
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
	requirePromptInput(*session, QStringLiteral("Name of the new file"));
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
	requirePromptInput(*session, QStringLiteral("New name for"));
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

TEST_CASE("Shift+F2 renames the selected files by a mask, numbering them in the panel's order, and Undo restores their names", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"));
	sandbox.createFile(QStringLiteral("work/b.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("b.txt"));
	session->send("*"sv);
	requireScreen(*session, [](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(QStringLiteral("2 files, 0 folders selected")); },
		QStringLiteral("both files selected"));

	session->send(Keys::ShiftF2);
	requireScreen(*session, [](const CTerminalScreen& screen) {
		const int maskRow = rowWith(screen, QStringLiteral("Name mask:"));
		return maskRow >= 0 && screen.cursorRow() == maskRow;
	}, QStringLiteral("the batch rename dialog, with the cursor in the name mask"));
	typeText(*session, QStringLiteral("x[C]")); // The mask starts selected, so this replaces it
	requireText(*session, QStringLiteral("x2.txt"));

	const auto filesAre = [&sandbox](std::initializer_list<const char*> names) {
		return std::ranges::all_of(names, [&sandbox](const char* name) { return QFile::exists(sandbox.work(QString::fromLatin1(name))); });
	};
	session->send(Keys::AltR);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return filesAre({ "x1.txt", "x2.txt" }) && screen.contains(QStringLiteral("Renamed")); },
		QStringLiteral("a.txt and b.txt renamed to x1.txt and x2.txt"));

	session->send(Keys::AltU);
	requireScreen(*session, [&](const CTerminalScreen&) { return filesAre({ "a.txt", "b.txt" }); }, QStringLiteral("the names restored"));

	session->send(Keys::Escape);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Name mask:")) && panelLists(screen, QStringLiteral("a.txt")); },
		QStringLiteral("the dialog closed, the panel listing a.txt"));
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

TEST_CASE("Alt+F7 finds files by name under the panel's folder, and Enter on a result goes to it", "[tui]")
{
	const Sandbox sandbox;
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("sub")));
	sandbox.createFile(QStringLiteral("work/sub/needle.txt"));
	sandbox.createFile(QStringLiteral("work/hay.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("hay.txt"));

	findByName(*session, QStringLiteral("needle"), QStringLiteral("Search completed, 1 item found"));
	const QString result = QDir::toNativeSeparators(QStringLiteral("sub/needle.txt")); // Relative to the folder searched in
	CHECK(session->screen().contains(result));
	CHECK(session->screen().contains(QDir::toNativeSeparators(sandbox.work(QStringLiteral("sub/needle.txt"))))); // The one under the cursor, in full

	session->send(Keys::Enter); // The results have the focus once the search completes
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Search for:")) && cursorIsOn(screen, QStringLiteral("needle.txt")); },
		QStringLiteral("the cursor on the result, in its folder"));

	// The dialog opens again with the results
	openFindDialog(*session);
	CHECK(session->screen().contains(QStringLiteral("Search completed, 1 item found")));
	CHECK(session->screen().contains(result));
}

TEST_CASE("F3 on a search result opens the pager on it", "[tui]")
{
	const Sandbox sandbox;
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("sub")));
	sandbox.createFile(QStringLiteral("work/sub/viewed.txt"));
	const QString recordPath = sandbox.path(QStringLiteral("record.txt"));
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("PAGER"), recordingCommand(recordPath) } } });
	goTo(*session, sandbox.work({}), QStringLiteral("sub"));

	findByName(*session, QStringLiteral("viewed"), QStringLiteral("Search completed, 1 item found"));
	session->send(Keys::F3);
	requireScreen(*session, [&](const CTerminalScreen&) { return QFile::exists(recordPath); }, QStringLiteral("the stub's record"));
	requireRecordedLaunch(recordedLines(recordPath), sandbox.work(QStringLiteral("sub/viewed.txt")));
}

TEST_CASE("Alt+F7 starts with the name last searched, in the next session too", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/needle.txt"));
	{
		auto session = startFcterm(sandbox);
		goTo(*session, sandbox.work({}), QStringLiteral("needle.txt"));
		findByName(*session, QStringLiteral("needle"), QStringLiteral("Search completed, 1 item found"));
		session->send(Keys::Escape);
		requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Search for:")); }, QStringLiteral("the dialog closed"));
		session->send(Keys::AltX);
		REQUIRE(session->waitForExit() == 0);
	}

	auto session = startFcterm(sandbox);
	const int nameRow = openFindDialog(*session);
	CHECK(session->screen().row(nameRow).contains(QStringLiteral("needle")));
}

TEST_CASE("Alt+F7 skips the folders in the Exclude folders field, which starts with its last text in the next session", "[tui]")
{
	const Sandbox sandbox;
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("kept")));
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("skipped")));
	sandbox.createFile(QStringLiteral("work/kept/needle.txt"));
	sandbox.createFile(QStringLiteral("work/skipped/needle.txt"));
	{
		auto session = startFcterm(sandbox);
		goTo(*session, sandbox.work({}), QStringLiteral("kept"));

		const int nameRow = openFindDialog(*session), excludedRow = rowWith(session->screen(), QStringLiteral("Exclude folders:"));
		session->send(Keys::AltE);
		requireScreen(*session, [excludedRow](const CTerminalScreen& screen) { return screen.cursorRow() == excludedRow; }, QStringLiteral("the cursor in the Exclude folders field"));
		typeText(*session, QStringLiteral("skipped"));
		session->send(Keys::AltF);
		requireScreen(*session, [nameRow](const CTerminalScreen& screen) { return screen.cursorRow() == nameRow; }, QStringLiteral("the cursor in the name field"));
		typeText(*session, QStringLiteral("needle"));
		session->send(Keys::Enter);
		requireText(*session, QStringLiteral("Search completed, 1 item found"));
		CHECK(session->screen().contains(QDir::toNativeSeparators(QStringLiteral("kept/needle.txt"))));

		session->send(Keys::Escape);
		requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Search for:")); }, QStringLiteral("the dialog closed"));
		session->send(Keys::AltX);
		REQUIRE(session->waitForExit() == 0);
	}

	auto session = startFcterm(sandbox);
	openFindDialog(*session);
	CHECK(session->screen().row(rowWith(session->screen(), QStringLiteral("Exclude folders:"))).contains(QStringLiteral("skipped")));
}

TEST_CASE("Alt+F7 without partial match matches whole names, and says when the contents regex is invalid", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/needle.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("needle.txt"));

	const int nameRow = openFindDialog(*session);
	REQUIRE(session->screen().contains(QStringLiteral("[X] Partial match")));
	const auto focusName = [&] {
		session->send(Keys::AltF);
		requireScreen(*session, [nameRow](const CTerminalScreen& screen) { return screen.cursorRow() == nameRow; }, QStringLiteral("the cursor in the name field"));
	};
	session->send(Keys::AltP);
	requireText(*session, QStringLiteral("[ ] Partial match"));
	focusName();
	typeText(*session, QStringLiteral("needle"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("Search completed, 0 items found"));
	typeText(*session, QStringLiteral(".txt"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("Search completed, 1 item found"));

	session->send(Keys::AltO);
	requireScreen(*session, [nameRow](const CTerminalScreen& screen) { return screen.cursorRow() > nameRow; }, QStringLiteral("the cursor in the contents field"));
	typeText(*session, QStringLiteral("("));
	session->send(Keys::AltR);
	requireText(*session, QStringLiteral("[X] Regex"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("The contents pattern is not a valid regular expression"));
}

TEST_CASE("Alt+F7 finds files by their contents", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/apple.txt"), "a red fruit");
	sandbox.createFile(QStringLiteral("work/banana.txt"), "a yellow fruit");
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("apple.txt"));

	const int nameRow = openFindDialog(*session);
	session->send(Keys::AltO); // The field's label key
	requireScreen(*session, [nameRow](const CTerminalScreen& screen) { return screen.cursorRow() != nameRow; }, QStringLiteral("the cursor in the contents field"));
	typeText(*session, QStringLiteral("yellow"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("Search completed, 1 item found"));
	CHECK(session->screen().contains(QStringLiteral("banana.txt")));
	CHECK_FALSE(session->screen().contains(QStringLiteral("apple.txt"))); // The dialog covers the panels

	// A letter typed in the results stays there: the check boxes take theirs without Alt from a view that leaves them
	session->send("r"sv);
	session->send(Keys::AltF);
	requireScreen(*session, [nameRow](const CTerminalScreen& screen) { return screen.cursorRow() == nameRow; }, QStringLiteral("the cursor back in the name field"));
	CHECK(session->screen().contains(QStringLiteral("[ ] Regex")));
}

TEST_CASE("Alt+F7 copes with searches too long for Turbo Vision's field history", "[tui]")
{
	const Sandbox sandbox;
	// The GUI saves any length: an entry holds every item selected for a search
	{
		const QString longEntry = QStringList(40, sandbox.work(QStringLiteral("folder"))).join(QStringLiteral("; "));
		QSettings settings{ sandbox.path(QStringLiteral("settings/GitHubSoft/File Commander.ini")), QSettings::IniFormat };
		settings.setValue(QStringLiteral("FileSearchDialog/Ui/RootFolder"), QStringList{ longEntry, sandbox.work({}) });
	}
	// Selected, their paths make a "Search in" text over 1 KiB, with the needle in the last of them
	for (int i = 0; i < 40; ++i)
		REQUIRE(QDir{ sandbox.work({}) }.mkdir(QStringLiteral("folder-%1").arg(i, 2, 10, QChar{ u'0' })));
	sandbox.createFile(QStringLiteral("work/folder-39/needle.txt"));

	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("folder-00"));
	session->send("*"sv);
	requireScreen(*session, [](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(QStringLiteral("0 files, 40 folders selected")); },
		QStringLiteral("every folder selected"));

	openFindDialog(*session);
	typeText(*session, QStringLiteral("needle"));
	session->send(Keys::Enter);
	requireText(*session, QStringLiteral("Search completed, 1 item found"));
	CHECK(session->screen().contains(QStringLiteral("needle.txt")));

	session->send(Keys::Escape);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Search for:")); }, QStringLiteral("the dialog closed"));
	session->send(Keys::AltX);
	CHECK(session->waitForExit() == 0);
}

TEST_CASE("Send to panel lists the search results in the panel, and Ctrl+G goes to a result's folder", "[tui]")
{
	const Sandbox sandbox;
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("sub")));
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("other")));
	sandbox.createFile(QStringLiteral("work/sub/needle.txt"));
	sandbox.createFile(QStringLiteral("work/other/needle.md"));
	sandbox.createFile(QStringLiteral("work/hay.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("hay.txt"));

	findByName(*session, QStringLiteral("needle"), QStringLiteral("Search completed, 2 items found"));
	session->send(Keys::AltN);
	// Shown relative to the folder holding them all
	const QString inSub = QDir::toNativeSeparators(QStringLiteral("sub/needle.txt")), inOther = QDir::toNativeSeparators(QStringLiteral("other/needle.md"));
	requireScreen(*session, [&](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Search for:")) && panelLists(screen, inSub) && panelLists(screen, inOther); },
		QStringLiteral("both results in the panel"));
	CHECK_FALSE(panelLists(session->screen(), QStringLiteral("hay.txt")));
	CHECK(session->screen().contains(QStringLiteral("Search results: ")));

	session->send(Keys::Home); // Sorted by extension: md first
	requireScreen(*session, [](const CTerminalScreen& screen) { return cursorIsOn(screen, QStringLiteral("needle.md")); }, QStringLiteral("the cursor on needle.md"));
	session->send(Keys::CtrlG);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return !panelLists(screen, inSub) && cursorIsOn(screen, QStringLiteral("needle.md")); },
		QStringLiteral("the cursor on needle.md in its folder"));
	CHECK_FALSE(session->screen().contains(QStringLiteral("Search results")));
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
		requirePromptInput(*session, QStringLiteral("Names matching"));
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

TEST_CASE("F10 and Alt+letter open the menus, whose commands act on the focused panel", "[tui]")
{
	const Sandbox sandbox;
	for (const char* name : { "a.txt", "b.txt", "c.doc" })
		sandbox.createFile(QStringLiteral("work/") + QString::fromLatin1(name));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("c.doc"));

	session->send(Keys::F10);
	session->send(Keys::Down);
	requireText(*session, QStringLiteral("Copy paths"));
	session->send(Keys::Escape);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("Copy paths")); }, QStringLiteral("the File menu closed"));

	session->send(Keys::AltS);
	requireText(*session, QStringLiteral("Invert selection"));
	session->send("i"sv);
	requireScreen(*session, [](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(QStringLiteral("3 files, 0 folders selected")); },
		QStringLiteral("every file selected"));
}

TEST_CASE("Tabs open, switch with their own filter, and close", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"));
	sandbox.createFile(QStringLiteral("other/b.txt"));
	sandbox.createFile(QStringLiteral("other/c.txt"));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("a.txt"));

	static constexpr int filterRow = panelStatusRow - 1;
	const auto tabStrip = [](const CTerminalScreen& screen) { return panelPart(screen.row(tabStripRow), Side::Left); };
	const auto filterLine = [](const CTerminalScreen& screen) { return panelPart(screen.row(filterRow), Side::Left).trimmed(); };

	session->send(Keys::CtrlT);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return tabStrip(screen).count(QStringLiteral(" work ")) == 2; }, QStringLiteral("two tabs"));
	goTo(*session, sandbox.path(QStringLiteral("other")), QStringLiteral("b.txt"));
	requireScreen(*session, [&](const CTerminalScreen& screen) { return tabStrip(screen).contains(QStringLiteral(" work  other ")); }, QStringLiteral("the tabs named"));

	session->send(Keys::CtrlF);
	requireScreen(*session, [](const CTerminalScreen& screen) { return screen.cursorRow() == filterRow; }, QStringLiteral("the cursor in the filter line"));
	typeText(*session, QStringLiteral("b*"));
	session->send(Keys::Enter);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !panelLists(screen, QStringLiteral("c.txt")); }, QStringLiteral("c.txt filtered out"));

	session->send(Keys::CtrlPgUp);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return panelLists(screen, QStringLiteral("a.txt")) && filterLine(screen) != QStringLiteral("b*"); },
		QStringLiteral("the previous tab, unfiltered"));
	session->send(Keys::Alt2);
	requireScreen(*session, [&](const CTerminalScreen& screen) {
		return panelLists(screen, QStringLiteral("b.txt")) && !panelLists(screen, QStringLiteral("c.txt")) && filterLine(screen) == QStringLiteral("b*");
	}, QStringLiteral("the second tab with its filter"));

	session->send(Keys::CtrlW);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return panelLists(screen, QStringLiteral("a.txt")) && !tabStrip(screen).contains(QStringLiteral(" other ")); },
		QStringLiteral("the other tab closed"));
}

TEST_CASE("The column header shows the sorting, and Ctrl+F6 or a click on a title changes it", "[tui]")
{
	const Sandbox sandbox;
	auto session = startFcterm(sandbox);
	const auto requireHeaderShows = [&](const QString& text) {
		requireScreen(*session, [&](const CTerminalScreen& screen) {
			const std::optional<int> row = columnHeaderRow(screen);
			return row && panelPart(screen.row(*row), Side::Left).contains(text);
		}, QStringLiteral("the column header to show \"%1\"").arg(text));
	};

	requireHeaderShows(QStringLiteral("Ext ↑")); // The core's default sorting
	requireHeaderShows(QStringLiteral("Size"));

	session->send(Keys::CtrlF6);
	requireHeaderShows(QStringLiteral("Size ↑"));
	session->send(Keys::CtrlF6);
	requireHeaderShows(QStringLiteral("Size ↓"));

	const int headerRow = *columnHeaderRow(session->screen());
	session->send(leftClick((int)session->screen().row(headerRow).indexOf(QStringLiteral("Name")), headerRow)); // The first one is the left panel's
	requireHeaderShows(QStringLiteral("Name ↑"));
}

TEST_CASE("A narrow panel drops columns, but never the sorted one", "[tui]")
{
	static constexpr int narrowScreenColumns = 60, narrowPanelWidth = narrowScreenColumns / 2;
	const Sandbox sandbox;
	auto session = startFcterm(sandbox, { .columns = narrowScreenColumns });
	const auto header = [](const CTerminalScreen& screen) {
		const std::optional<int> row = columnHeaderRow(screen, narrowPanelWidth);
		return row ? screen.row(*row).mid(1, narrowPanelWidth - 2) : QString{};
	};

	requireScreen(*session, [&](const CTerminalScreen& screen) { return header(screen).contains(QStringLiteral("Ext ↑")); }, QStringLiteral("the Ext title"));
	CHECK(!header(session->screen()).contains(QStringLiteral("Size")));
	CHECK(!header(session->screen()).contains(QStringLiteral("Date")));

	session->send(Keys::CtrlF5);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return header(screen).contains(QStringLiteral("Date ↑")); }, QStringLiteral("the Date title"));
	CHECK(!header(session->screen()).contains(QStringLiteral("Ext")));
	CHECK(!header(session->screen()).contains(QStringLiteral("Size")));
}

TEST_CASE("The Size column shows bytes below 1K, else the largest binary unit reached", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/bytes.txt"), QByteArray(1023, 'x'));
	sandbox.createFile(QStringLiteral("work/kib.txt"), QByteArray(1536, 'x'));
	sandbox.createFile(QStringLiteral("work/tens.txt"), QByteArray(12345, 'x'));
	auto session = startFcterm(sandbox);
	goTo(*session, sandbox.work({}), QStringLiteral("bytes.txt"));

	for (const auto& [baseName, sizeText] : { std::pair{ "bytes", "1023" }, std::pair{ "kib", "1.5K" }, std::pair{ "tens", "12K" } })
	{
		const QRegularExpression rowPattern{ QStringLiteral("^%1 +txt +%2(?: |$)").arg(QLatin1String(baseName), QRegularExpression::escape(QLatin1String(sizeText))) };
		requireScreen(*session, [&](const CTerminalScreen& screen) { return panelHasRowMatching(screen, rowPattern); },
			QStringLiteral("%1.txt listed with the size %2").arg(QLatin1String(baseName), QLatin1String(sizeText)));
	}
}

TEST_CASE("fcterm shows the working folder or its folder argument in the tab already showing it, else in the active tab", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"));
	sandbox.createFile(QStringLiteral("other/b.txt"));
	const auto tabStrip = [](const CTerminalScreen& screen) { return panelPart(screen.row(tabStripRow), Side::Left).trimmed(); };
	// Starts fcterm in the work folder and waits for listedName on the left side
	const auto start = [&](const QStringList& folderArgument, const QString& listedName) {
		auto session = startFcterm(sandbox, { .workingFolder = sandbox.work({}), .extraArguments = folderArgument });
		requireScreen(*session, [&](const CTerminalScreen& screen) { return panelLists(screen, listedName); }, QStringLiteral("%1 listed").arg(listedName));
		return session;
	};
	const auto quit = [](CTerminalSession& session) {
		session.send(Keys::AltX);
		REQUIRE(session.waitForExit() == 0);
	};

	auto session = start({}, QStringLiteral("a.txt"));
	session->send(Keys::CtrlT);
	requireScreen(*session, [&](const CTerminalScreen& screen) { return tabStrip(screen) == QStringLiteral("work  work"); }, QStringLiteral("two work tabs"));
	quit(*session);

	session = start({ QStringLiteral("../other") }, QStringLiteral("b.txt"));
	CHECK(tabStrip(session->screen()) == QStringLiteral("work  other"));
	quit(*session);

	session = start({}, QStringLiteral("a.txt"));
	CHECK(tabStrip(session->screen()) == QStringLiteral("work  other"));
	quit(*session);
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
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("EDITOR"), QStringLiteral("no-such-editor-for-tui-test") } } });
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

TEST_CASE("Ctrl+O runs the shell in the panel's folder until it exits", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"));
#ifdef _WIN32
	// The shell is COMSPEC, cmd.exe
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("PROMPT"), QStringLiteral("shell-ready$G") } } });
	const QString printFolder = QStringLiteral("cd > folder.txt");
#else
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("SHELL"), QStringLiteral("/bin/sh") }, { QStringLiteral("PS1"), QStringLiteral("shell-ready>") } } });
	const QString printFolder = QStringLiteral("pwd > folder.txt");
#endif
	goTo(*session, sandbox.work({}), QStringLiteral("a.txt"));

	session->send(Keys::CtrlO);
	requireText(*session, QStringLiteral("shell-ready>"));
	typeText(*session, printFolder);
	session->send(Keys::Enter);
	const QString folderFile = sandbox.work(QStringLiteral("folder.txt"));
	requireScreen(*session, [&](const CTerminalScreen&) { return QFileInfo{ folderFile }.size() > 0; }, QStringLiteral("the shell's folder written"));
	typeText(*session, QStringLiteral("exit"));
	session->send(Keys::Enter);
	// fcterm redraws the whole screen when it takes the terminal back
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.contains(QStringLiteral("shell-ready>")) && screen.contains(QStringLiteral("F2 Rename")); },
		QStringLiteral("fcterm back"));

	QFile folder{ folderFile };
	REQUIRE(folder.open(QFile::ReadOnly));
	CHECK(canonical(QString::fromLocal8Bit(folder.readAll()).trimmed()) == canonical(sandbox.work({})));
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
	std::map<QString, QString> noDesktop; // Linux: no display, which startFcterm already ensures
#ifdef __APPLE__
	// macOS: a remote session
	noDesktop[QStringLiteral("SSH_CONNECTION")] = QStringLiteral("192.0.2.1 50000 192.0.2.2 22");
#endif
	requireRecordedLaunch(recordedLaunch(sandbox, "PAGER", QStringLiteral("document.md"), Keys::Enter, noDesktop), sandbox.work(QStringLiteral("document.md")));
}

TEST_CASE("Alt+C over SSH copies the paths through the terminal", "[tui]")
{
	const Sandbox sandbox;
	sandbox.createFile(QStringLiteral("work/a.txt"));
	sandbox.createFile(QStringLiteral("work/with space.txt"));
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("SSH_CONNECTION"), QStringLiteral("192.0.2.1 50000 192.0.2.2 22") } } });
	goTo(*session, sandbox.work({}), QStringLiteral("with space.txt"));
	moveCursorTo(*session, QStringLiteral("a.txt"));

	// One shell word per line: a path that needs quoting is in single quotes
	const auto clipboardPaths = [&session] {
		QStringList paths;
		for (const QString& line : session->screen().clipboardText().split(u'\n'))
			paths.push_back(canonical(line.startsWith(u'\'') && line.endsWith(u'\'') ? line.mid(1, line.size() - 2) : line));
		return paths;
	};

	session->send(Keys::AltC);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.clipboardText().isEmpty(); }, QStringLiteral("the current item's path in the clipboard"));
	CHECK(clipboardPaths() == QStringList{ canonical(sandbox.work(QStringLiteral("a.txt"))) });

	session->send(Keys::Insert);
	session->send(Keys::Insert);
	requireScreen(*session, [](const CTerminalScreen& screen) { return panelStatus(screen).startsWith(QStringLiteral("2 files, 0 folders selected")); },
		QStringLiteral("both files selected"));
	session->send(Keys::AltC);
	requireScreen(*session, [](const CTerminalScreen& screen) { return screen.clipboardText().contains(u'\n'); }, QStringLiteral("both paths in the clipboard"));
	CHECK(session->screen().clipboardText().endsWith(u'\''));
	CHECK(clipboardPaths() == QStringList{ canonical(sandbox.work(QStringLiteral("a.txt"))), canonical(sandbox.work(QStringLiteral("with space.txt"))) });
}

TEST_CASE("Alt+C over SSH copies the search result's path through the terminal", "[tui]")
{
	const Sandbox sandbox;
	REQUIRE(QDir{ sandbox.work({}) }.mkpath(QStringLiteral("sub")));
	sandbox.createFile(QStringLiteral("work/sub/with space.txt"));
	auto session = startFcterm(sandbox, { .variables = { { QStringLiteral("SSH_CONNECTION"), QStringLiteral("192.0.2.1 50000 192.0.2.2 22") } } });
	goTo(*session, sandbox.work({}), QStringLiteral("sub"));

	findByName(*session, QStringLiteral("space"), QStringLiteral("Search completed, 1 item found"));
	session->send(Keys::AltC);
	requireScreen(*session, [](const CTerminalScreen& screen) { return !screen.clipboardText().isEmpty(); }, QStringLiteral("the result's path in the clipboard"));
	const QString copied = session->screen().clipboardText();
	REQUIRE((copied.startsWith(u'\'') && copied.endsWith(u'\'')));
	CHECK(canonical(copied.mid(1, copied.size() - 2)) == canonical(sandbox.work(QStringLiteral("sub/with space.txt"))));
}
#endif

#include "userprograms/userprograms.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

// Quoting is shellQuotedPath's contract; these tests check that each value goes through it
static QString quoted(const QString& path)
{
	return shellQuotedPath(toNativeSeparators(path));
}

static PlaceholderValues panelState()
{
	return {
		.currentDir = QStringLiteral("/work/"),
		.currentItem = QStringLiteral("/work/a.txt"),
		.selection = { QStringLiteral("/work/a.txt"), QStringLiteral("/work/b.txt") },
		.otherDir = QStringLiteral("/other/"),
		.otherItem = QStringLiteral("/other/c.txt"),
	};
}

TEST_CASE("Text without placeholders is unchanged", "[userprograms]")
{
	const QString commandLine = QStringLiteral("find . -exec echo {} ; {unknown} {FILE} {file");
	CHECK(expandPlaceholders(commandLine, panelState()) == commandLine);
}

TEST_CASE("Every placeholder expands to its value", "[userprograms]")
{
	const auto expanded = expandPlaceholders(QStringLiteral("tool {file} {name} {sel} {dir} {other} {otherfile}"), panelState());
	REQUIRE(expanded);
	CHECK(*expanded == QString{ QStringLiteral("tool ") % quoted(QStringLiteral("/work/a.txt")) % QStringLiteral(" a.txt ")
		% quoted(QStringLiteral("/work/a.txt")) % ' ' % quoted(QStringLiteral("/work/b.txt")) % ' '
		% quoted(QStringLiteral("/work")) % ' ' % quoted(QStringLiteral("/other")) % ' ' % quoted(QStringLiteral("/other/c.txt")) });
}

TEST_CASE("placeholderToken yields the text expandPlaceholders recognizes", "[userprograms]")
{
	for (const Placeholder placeholder : { Placeholder::File, Placeholder::Name, Placeholder::Selection, Placeholder::Dir, Placeholder::Other, Placeholder::OtherFile })
	{
		const QString token = placeholderToken(placeholder);
		CAPTURE(token);
		const auto expanded = expandPlaceholders(token, panelState());
		REQUIRE(expanded);
		CHECK(*expanded != token);
	}
}

TEST_CASE("Adjacent and repeated placeholders all expand", "[userprograms]")
{
	CHECK(expandPlaceholders(QStringLiteral("{name}{name}-{name}"), panelState()) == QStringLiteral("a.txta.txt-a.txt"));
}

TEST_CASE("A folder's name is its last component", "[userprograms]")
{
	PlaceholderValues values = panelState();
	values.currentItem = QStringLiteral("/work/sub/");
	CHECK(expandPlaceholders(QStringLiteral("{name}"), values) == QStringLiteral("sub"));
}

TEST_CASE("Values are quoted each on its own", "[userprograms]")
{
	PlaceholderValues values = panelState();
	values.selection = { QStringLiteral("/work/a b.txt"), QStringLiteral("/work/c&d.txt") };

	const auto expanded = expandPlaceholders(QStringLiteral("{sel}"), values);
	REQUIRE(expanded);
	CHECK(*expanded == QString{ quoted(QStringLiteral("/work/a b.txt")) % ' ' % quoted(QStringLiteral("/work/c&d.txt")) });
	CHECK(*expanded != toNativeSeparators(QStringLiteral("/work/a b.txt /work/c&d.txt")));
}

TEST_CASE("A value containing a placeholder is not expanded again", "[userprograms]")
{
	PlaceholderValues values = panelState();
	values.currentItem = QStringLiteral("/work/{dir}.txt");
	CHECK(expandPlaceholders(QStringLiteral("{file}"), values) == quoted(QStringLiteral("/work/{dir}.txt")));
}

TEST_CASE("A placeholder without a value fails the expansion", "[userprograms]")
{
	PlaceholderValues values = panelState();
	values.currentItem.clear();
	values.selection.clear();
	values.otherItem.clear();

	CHECK(expandPlaceholders(QStringLiteral("x {file}"), values) == std::unexpected{ PlaceholderError::NoCurrentItem });
	CHECK(expandPlaceholders(QStringLiteral("x {name}"), values) == std::unexpected{ PlaceholderError::NoCurrentItem });
	CHECK(expandPlaceholders(QStringLiteral("x {sel}"), values) == std::unexpected{ PlaceholderError::NoCurrentItem });
	CHECK(expandPlaceholders(QStringLiteral("x {otherfile}"), values) == std::unexpected{ PlaceholderError::NoOtherItem });
	CHECK(expandPlaceholders(QStringLiteral("x {dir} {other}"), values).has_value());
}

TEST_CASE("The working folder follows the program's setting", "[userprograms]")
{
	UserProgram program{ .name = {}, .commandLine = QStringLiteral("x"), .workingDir = UserProgram::WorkingDir::CurrentPanel, .customWorkingDir = QStringLiteral("/custom"), .editBeforeRunning = false };
	CHECK(workingDirFor(program, panelState()) == toNativeSeparators(QStringLiteral("/work/")));

	program.workingDir = UserProgram::WorkingDir::OtherPanel;
	CHECK(workingDirFor(program, panelState()) == toNativeSeparators(QStringLiteral("/other/")));

	program.workingDir = UserProgram::WorkingDir::Custom;
	CHECK(workingDirFor(program, panelState()) == toNativeSeparators(QStringLiteral("/custom")));
}

TEST_CASE("Programs survive a save and load", "[userprograms]")
{
	const std::vector<UserProgram> programs{
		{ .name = QStringLiteral("Diff"), .commandLine = QStringLiteral("diff {file} {otherfile}"), .workingDir = UserProgram::WorkingDir::OtherPanel, .customWorkingDir = {}, .editBeforeRunning = true },
		{ .name = QStringLiteral("Build"), .commandLine = QStringLiteral("make"), .workingDir = UserProgram::WorkingDir::Custom, .customWorkingDir = QStringLiteral("/src"), .editBeforeRunning = false },
		{ .name = QStringLiteral("Notes"), .commandLine = QStringLiteral("notepad"), .workingDir = UserProgram::WorkingDir::CurrentPanel, .customWorkingDir = {}, .editBeforeRunning = false },
	};

	saveUserPrograms(programs);
	CHECK(loadUserPrograms() == programs);

	// A shorter list replaces the longer one entirely
	saveUserPrograms({ programs[1] });
	CHECK(loadUserPrograms() == std::vector<UserProgram>{ programs[1] });

	saveUserPrograms({});
	CHECK(loadUserPrograms().empty());
}

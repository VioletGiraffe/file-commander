#include "batchrename/cbatchrenamesession.h"

#include "temptree.h"


// Submodule includes
#include "qtcore_helpers/catch_qt.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

#include <utility>
#include <vector>

using Session = CBatchRenameSession;

namespace {

[[nodiscard]] std::vector<CFileSystemObject> itemsAt(const QStringList& paths)
{
	std::vector<CFileSystemObject> items;
	for (const QString& path : paths)
		items.emplace_back(path);
	return items;
}

[[nodiscard]] BatchRenameRules nameMask(const QString& mask)
{
	BatchRenameRules rules;
	rules.nameMask = mask;
	return rules;
}

const auto always = [] { return true; };

} // namespace

TEST_CASE("Batch rename session - the default rules change nothing, and there is nothing to apply", "[batchrename]")
{
	TempTree tree;
	const Session session{ itemsAt({ tree.makeFile(QSL("a.txt")), tree.makeDir(QSL("folder.v2")) }) };

	REQUIRE(session.rows().size() == 2);
	for (const Session::Row& row : session.rows())
	{
		CHECK(row.newName == row.path.name());
		CHECK(row.state == Session::PreviewState::Unchanged);
		CHECK(Session::statusOf(row).tone == Session::Tone::Dimmed);
	}
	CHECK_FALSE(session.canRun(Session::Step::Apply));
	CHECK_FALSE(session.canRun(Session::Step::Undo));
}

TEST_CASE("Batch rename session - new names follow the rules, the counter following the items' order", "[batchrename]")
{
	TempTree tree;
	const QString b = tree.makeFile(QSL("b.txt"));
	const QString a = tree.makeFile(QSL("a.txt"));
	Session session{ itemsAt({ b, a }) };

	session.setRules(nameMask(QSL("item[C]")));
	CHECK(session.rows()[0].newName == QSL("item1.txt"));
	CHECK(session.rows()[1].newName == QSL("item2.txt"));
	CHECK(session.rows()[0].state == Session::PreviewState::Renamable);
	CHECK(Session::statusOf(session.rows()[0]).text.isEmpty());
	CHECK(session.canRun(Session::Step::Apply));
	CHECK(session.pathsRenamedBy(Session::Step::Apply) == QStringList{ b, a });
}

TEST_CASE("Batch rename session - an invalid or shared new name blocks the batch", "[batchrename]")
{
	TempTree tree;
	Session session{ itemsAt({ tree.makeFile(QSL("a.txt")), tree.makeFile(QSL("b.txt")) }) };

	SECTION("Two items getting the same name")
	{
		session.setRules(nameMask(QSL("same")));
		for (const Session::Row& row : session.rows())
		{
			CHECK(row.state == Session::PreviewState::NameTaken);
			CHECK(Session::statusOf(row).tone == Session::Tone::Error);
		}
		CHECK_FALSE(session.canRun(Session::Step::Apply));
	}

	SECTION("A name that is not a usable entry name")
	{
		session.setRules(nameMask(QSL("a/[N]")));
		CHECK(session.rows()[0].state == Session::PreviewState::InvalidName);
		CHECK(session.rows()[0].nameRejection != NameRejection::None);
		CHECK_FALSE(Session::statusOf(session.rows()[0]).text.isEmpty());
		CHECK_FALSE(session.canRun(Session::Step::Apply));
	}

	SECTION("A find regex that does not compile")
	{
		BatchRenameRules rules = nameMask(QSL("[N]2"));
		rules.find = QSL("(");
		rules.findIsRegex = true;
		session.setRules(rules);
		CHECK_FALSE(session.findPatternError().isEmpty());
		CHECK_FALSE(session.canRun(Session::Step::Apply));
	}
}

#if defined(_WIN32) || defined(__APPLE__)
TEST_CASE("Batch rename session - names differing only in case are the same name where filesystems ignore case", "[batchrename]")
{
	TempTree tree;
	Session session{ itemsAt({ tree.makeFile(QSL("File.txt")), tree.makeFile(QSL("file2.txt")) }) };

	BatchRenameRules rules;
	rules.find = QSL("2");
	session.setRules(rules);
	CHECK(session.rows()[0].state == Session::PreviewState::Unchanged);
	CHECK(session.rows()[1].newName == QSL("file.txt"));
	CHECK(session.rows()[1].state == Session::PreviewState::NameTaken);
}
#endif

TEST_CASE("Batch rename session - applying renames the items, and undo restores their names", "[batchrename]")
{
	TempTree tree;
	const QString a = tree.makeFile(QSL("a.txt"));
	const QString b = tree.makeFile(QSL("b.txt"));
	Session session{ itemsAt({ a, b }) };

	const QString aRenamed = tree.path(QSL("a_x.txt")), bRenamed = tree.path(QSL("b_x.txt"));
	using RenamedPaths = std::vector<std::pair<QString, QString>>;
	const auto pathOf = [](const QString& path) { return CFileSystemObject{ path }.fullAbsolutePath(); };

	session.setRules(nameMask(QSL("[N]_x")));
	const RenamedPaths applied = session.run(Session::Step::Apply, always);
	CHECK(applied == RenamedPaths{ { pathOf(a), pathOf(aRenamed) }, { pathOf(b), pathOf(bRenamed) } });
	CHECK(QFile::exists(tree.path(QSL("a_x.txt"))));
	CHECK(QFile::exists(tree.path(QSL("b_x.txt"))));
	CHECK_FALSE(QFile::exists(a));
	for (const Session::Row& row : session.rows())
	{
		CHECK(row.outcome == Session::Outcome::Renamed);
		CHECK(Session::statusOf(row).text == QSL("Renamed"));
	}
	CHECK(session.rows()[0].path.name() == QSL("a_x.txt"));
	CHECK(session.rows()[0].newName == QSL("a_x_x.txt")); // The rules again, on the new names
	CHECK_FALSE(session.canRun(Session::Step::Apply)); // Until the rules change
	REQUIRE(session.canRun(Session::Step::Undo));

	const RenamedPaths undone = session.run(Session::Step::Undo, always);
	CHECK(undone == RenamedPaths{ { pathOf(aRenamed), pathOf(a) }, { pathOf(bRenamed), pathOf(b) } });
	CHECK(QFile::exists(a));
	CHECK(QFile::exists(b));
	CHECK(session.rows()[0].path.name() == QSL("a.txt"));
	CHECK_FALSE(session.canRun(Session::Step::Undo));

	session.setRules(nameMask(QSL("[N]_y")));
	CHECK(session.rows()[0].outcome == Session::Outcome::None);
	CHECK(session.canRun(Session::Step::Apply));
}

TEST_CASE("Batch rename session - a stopped batch marks the items it did not reach, and leaves nothing to undo", "[batchrename]")
{
	TempTree tree;
	const QString a = tree.makeFile(QSL("a.txt"));
	Session session{ itemsAt({ a }) };

	session.setRules(nameMask(QSL("[N]_x")));
	session.run(Session::Step::Apply, [] { return false; });
	CHECK(QFile::exists(a));
	CHECK(session.rows()[0].outcome == Session::Outcome::Cancelled);
	CHECK_FALSE(session.canRun(Session::Step::Undo));
}

TEST_CASE("Batch rename session - an item whose new name is taken outside the batch fails, and says why", "[batchrename]")
{
	TempTree tree;
	const QString a = tree.makeFile(QSL("a.txt"));
	tree.makeFile(QSL("b.txt"));
	Session session{ itemsAt({ a }) };

	session.setRules(nameMask(QSL("b")));
	REQUIRE(session.canRun(Session::Step::Apply));
	session.run(Session::Step::Apply, always);
	CHECK(QFile::exists(a));
	CHECK(session.rows()[0].outcome == Session::Outcome::Failed);
	CHECK(session.rows()[0].failure.has_value());
	CHECK(Session::statusOf(session.rows()[0]).tone == Session::Tone::Error);
	CHECK_FALSE(session.canRun(Session::Step::Undo));
}

TEST_CASE("Batch rename session - the saved options come back, and the masks start at the defaults", "[batchrename]")
{
	BatchRenameRules rules = nameMask(QSL("[N]_x"));
	rules.findIsRegex = true;
	rules.findIsCaseSensitive = true;
	rules.counterStart = 7;
	rules.counterStep = 3;
	rules.counterDigits = 4;
	Session::saveOptions(rules);

	const BatchRenameRules initial = Session::initialRules();
	CHECK(initial.findIsRegex);
	CHECK(initial.findIsCaseSensitive);
	CHECK(initial.counterStart == 7);
	CHECK(initial.counterStep == 3);
	CHECK(initial.counterDigits == 4);
	CHECK(initial.nameMask == BatchRenameRules{}.nameMask);
}

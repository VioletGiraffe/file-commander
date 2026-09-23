#include "ccsvcommentlistmodel.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

using Strings = std::vector<std::string>;

namespace {

// The table and the list it is built from, since the list holds a reference to the table
struct Fixture
{
	explicit Fixture(const std::string& text)
	{
		table.setTable(parseCsv(QString::fromStdString(text), u',', true));
		list.refresh();
	}

	CCsvTableModel table;
	CCsvCommentListModel list{ table };
};

} // namespace

static Strings entriesOf(const CCsvCommentListModel& list)
{
	Strings entries;
	for (int row = 0; row < list.rowCount(); ++row)
		entries.push_back(list.index(row, 0).data().toString().toStdString());
	return entries;
}

TEST_CASE("CCsvCommentListModel: entries", "[csv][comments][navigation]")
{
	SECTION("One per comment row, in file order, with its text")
	{
		const Fixture fixture("#head\na,1\n# section\nb,2\n#tail");
		CHECK(entriesOf(fixture.list) == Strings{ "#head", "# section", "#tail" });
		CHECK(fixture.list.tableRow(0) == 0);
		CHECK(fixture.list.tableRow(1) == 2);
		CHECK(fixture.list.tableRow(2) == 4);
	}

	SECTION("Empty without comment rows")
	{
		CHECK(entriesOf(Fixture("a,1\nb,2").list).empty());
		CHECK(entriesOf(Fixture("").list).empty());
	}

	SECTION("Empty while the table is sorted, filled again when the sort is cleared")
	{
		Fixture fixture("#head\n3\n#section\n1");

		fixture.table.sort(0, Qt::AscendingOrder);
		fixture.list.refresh();
		CHECK(fixture.list.rowCount() == 0);

		fixture.table.sort(-1, Qt::AscendingOrder);
		fixture.list.refresh();
		CHECK(entriesOf(fixture.list) == Strings{ "#head", "#section" });
	}

	SECTION("Rows shift when the header row leaves the table")
	{
		Fixture fixture("name,score\n#note\nbob,7");
		CHECK(fixture.list.tableRow(0) == 1);

		fixture.table.setFirstRowIsHeader(true);
		fixture.list.refresh();
		CHECK(entriesOf(fixture.list) == Strings{ "#note" });
		CHECK(fixture.list.tableRow(0) == 0);
	}
}

TEST_CASE("CCsvCommentListModel: navigation", "[csv][comments][navigation]")
{
	// Comment rows at 0, 3 and 5
	const Fixture fixture("#a\nx\ny\n#b\nz\n#c");
	const CCsvCommentListModel& list = fixture.list;

	SECTION("The next comment after a row")
	{
		CHECK(list.commentRowAfter(-1) == 0); // Nothing selected: the first comment is next
		CHECK(list.commentRowAfter(0) == 3);
		CHECK(list.commentRowAfter(1) == 3);
		CHECK(list.commentRowAfter(3) == 5);
		CHECK(list.commentRowAfter(5) == -1);
		CHECK(list.commentRowAfter(99) == -1);
	}

	SECTION("The previous comment before a row")
	{
		CHECK(list.commentRowBefore(0) == -1);
		CHECK(list.commentRowBefore(1) == 0);
		CHECK(list.commentRowBefore(3) == 0);
		CHECK(list.commentRowBefore(4) == 3);
		CHECK(list.commentRowBefore(5) == 3);
		CHECK(list.commentRowBefore(99) == 5);
	}

	SECTION("Neither direction finds anything in an empty list")
	{
		const Fixture noComments("a\nb");
		CHECK(noComments.list.commentRowAfter(-1) == -1);
		CHECK(noComments.list.commentRowBefore(99) == -1);
	}
}

#include "ccsvtablemodel.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"

#include <QObject>
#include <QPersistentModelIndex>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

using Strings = std::vector<std::string>;

static CsvTable parse(const std::string& text, bool recognizeCommentLines = false)
{
	return parseCsv(QString::fromStdString(text), u',', recognizeCommentLines);
}

static std::string displayText(const CCsvTableModel& model, int row, int column)
{
	return model.index(row, column).data().toString().toStdString();
}

static std::string horizontalHeaderOf(const CCsvTableModel& model, int column)
{
	return model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString().toStdString();
}

static Strings columnOf(const CCsvTableModel& model, int column)
{
	Strings values;
	for (int row = 0; row < model.rowCount(); ++row)
		values.push_back(displayText(model, row, column));
	return values;
}

static Strings verticalHeadersOf(const CCsvTableModel& model)
{
	Strings headers;
	for (int row = 0; row < model.rowCount(); ++row)
		headers.push_back(model.headerData(row, Qt::Vertical, Qt::DisplayRole).toString().toStdString());
	return headers;
}

static std::vector<int> commentRowsOf(const CCsvTableModel& model)
{
	std::vector<int> rows;
	for (int row = 0; row < model.rowCount(); ++row)
	{
		if (model.isCommentRow(row))
			rows.push_back(row);
	}
	return rows;
}

static bool looksLikeHeader(const std::string& text, bool recognizeCommentLines = false)
{
	CCsvTableModel model;
	model.setTable(parse(text, recognizeCommentLines));
	return model.firstRowLooksLikeHeader();
}

static std::string repeated(const std::string& line, int count)
{
	std::string result;
	for (int i = 0; i < count; ++i)
		result += line;
	return result;
}

TEST_CASE("CCsvTableModel: header row detection", "[csv][model][header]")
{
	SECTION("Text over a numeric column")
	{
		CHECK(looksLikeHeader("name,age\nbob,30\nann,25"));
		CHECK(looksLikeHeader("name,city,score\nbob,paris,7"));
	}

	SECTION("Nothing to confirm a header with")
	{
		CHECK_FALSE(looksLikeHeader(""));
		CHECK_FALSE(looksLikeHeader("name,age"));
		CHECK_FALSE(looksLikeHeader("name,age\n\n\n"));
		CHECK_FALSE(looksLikeHeader("name,city\nbob,paris\nann,rome"));
	}

	SECTION("An empty header cell")
	{
		CHECK_FALSE(looksLikeHeader("name,,age\nbob,x,30"));
		// A header row shorter than the table lacks the trailing cells
		CHECK_FALSE(looksLikeHeader("a\nb,1\nc,2"));
	}

	SECTION("Empty and missing cells do not disqualify a numeric column")
	{
		CHECK(looksLikeHeader("name,score\nbob,\nann,7"));
		CHECK(looksLikeHeader("name,score\nbob\nann,7"));
	}

	SECTION("A column needs at least one number")
	{
		CHECK_FALSE(looksLikeHeader("name,score\nbob,\nann,"));
	}

	SECTION("A single non-number disqualifies a column")
	{
		CHECK_FALSE(looksLikeHeader("name,score\nbob,7\nann,n/a"));
	}

	SECTION("Number formats")
	{
		CHECK(looksLikeHeader("name,score\nbob,-1.5e3\nann, 7 \ncid,+0"));
		CHECK_FALSE(looksLikeHeader("name,score\nbob,\"1,5\""));
		CHECK_FALSE(looksLikeHeader("name,score\nbob,0x10"));
	}

	SECTION("Only the first 100 data rows are sampled")
	{
		CHECK_FALSE(looksLikeHeader("name,score\n" + repeated("x,1\n", 99) + "x,oops\n"));
		CHECK(looksLikeHeader("name,score\n" + repeated("x,1\n", 100) + "x,oops\n"));
	}
}

TEST_CASE("CCsvTableModel: first row as header", "[csv][model][header]")
{
	CCsvTableModel model;
	model.setTable(parse("name,age\nbob,30\nann,25"));

	CHECK(model.rowCount() == 3);
	CHECK(horizontalHeaderOf(model, 0) == "1");

	model.setFirstRowIsHeader(true);
	CHECK(model.rowCount() == 2);
	CHECK(horizontalHeaderOf(model, 0) == "name");
	CHECK(horizontalHeaderOf(model, 1) == "age");
	CHECK(columnOf(model, 0) == Strings{ "bob", "ann" });
	// File row numbers: the header is row 1
	CHECK(verticalHeadersOf(model) == Strings{ "2", "3" });

	model.setFirstRowIsHeader(false);
	CHECK(columnOf(model, 0) == Strings{ "name", "bob", "ann" });
	CHECK(verticalHeadersOf(model) == Strings{ "1", "2", "3" });

	SECTION("On an empty table")
	{
		CCsvTableModel emptyModel;
		emptyModel.setTable(parse(""));
		emptyModel.setFirstRowIsHeader(true);
		CHECK(emptyModel.rowCount() == 0);
		CHECK(emptyModel.columnCount() == 0);
	}
}

TEST_CASE("CCsvTableModel: sorting", "[csv][model][sort]")
{
	CCsvTableModel model;

	SECTION("Numbers compare numerically")
	{
		model.setTable(parse("10\n9\n100\n-2"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "-2", "9", "10", "100" });
	}

	SECTION("Numbers precede text, text ignores case")
	{
		model.setTable(parse("b\nA\n2\nc\n1"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "2", "A", "b", "c" });
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "c", "b", "A", "2", "1" });
	}

	SECTION("Number formats: exponent, explicit sign, padding, infinities")
	{
		model.setTable(parse("1e3\n-5\n+2\n0.5\n 7 \ninf\n-inf"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "-inf", "-5", "0.5", "+2", " 7 ", "1e3", "inf" });
	}

	SECTION("NaN is text: it has no order among numbers")
	{
		model.setTable(parse("3\nnan\n1\nNaN\n2"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "2", "3", "nan", "NaN" });
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "nan", "NaN", "3", "2", "1" });
	}

	SECTION("Stable in both directions: equal keys keep file order")
	{
		model.setTable(parse("1,a\n2,b\n1,c\n2,d\nx,e\nX,f"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 1) == Strings{ "a", "c", "b", "d", "e", "f" });
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 1) == Strings{ "e", "f", "b", "d", "a", "c" });
	}

	SECTION("Empty cells sort last in both directions")
	{
		model.setTable(parse("3,x\n,y\nb,z\n1,w"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "3", "b", "" });
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "b", "3", "1", "" });
	}

	SECTION("Empty cells keep file order among themselves")
	{
		model.setTable(parse("a,1\n,2\n,3"));
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 1) == Strings{ "1", "2", "3" });
	}

	SECTION("Row numbers follow the rows; column -1 restores file order")
	{
		model.setTable(parse("3\n1\n2"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(verticalHeadersOf(model) == Strings{ "2", "3", "1" });

		model.sort(-1, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "3", "1", "2" });
		CHECK(verticalHeadersOf(model) == Strings{ "1", "2", "3" });
	}

	SECTION("The header row stays out of the sort")
	{
		model.setTable(parse("val\n3\n1\n2"));
		model.setFirstRowIsHeader(true);
		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "3", "2", "1" });
		CHECK(verticalHeadersOf(model) == Strings{ "2", "4", "3" });
		CHECK(horizontalHeaderOf(model, 0) == "val");
	}

	SECTION("Changing the header row or the table drops the sort")
	{
		model.setTable(parse("val\n3\n1\n2"));
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "2", "3", "val" });

		model.setFirstRowIsHeader(true);
		CHECK(columnOf(model, 0) == Strings{ "3", "1", "2" });

		// The header setting outlives the table
		model.sort(0, Qt::AscendingOrder);
		model.setTable(parse("c\nb\na"));
		CHECK(columnOf(model, 0) == Strings{ "b", "a" });
	}

	SECTION("Sorting by a column that short rows do not reach")
	{
		model.setTable(parse("x,2\ny\nz,1"));
		model.sort(1, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "z", "x", "y" });
	}
}

TEST_CASE("CCsvTableModel: persistent indexes follow their rows through sorting", "[csv][model][sort]")
{
	CCsvTableModel model;
	model.setTable(parse("k,v\n3,x\n1,y\n2,z"));
	model.setFirstRowIsHeader(true);

	const QPersistentModelIndex xCell{ model.index(0, 1) };
	const QPersistentModelIndex twoCell{ model.index(2, 0) };

	model.sort(0, Qt::AscendingOrder);
	CHECK(xCell.row() == 2);
	CHECK(xCell.column() == 1);
	CHECK(xCell.data().toString().toStdString() == "x");
	CHECK(twoCell.row() == 1);
	CHECK(twoCell.data().toString().toStdString() == "2");

	model.sort(0, Qt::DescendingOrder);
	CHECK(xCell.row() == 0);
	CHECK(twoCell.row() == 1);

	model.sort(-1, Qt::AscendingOrder);
	CHECK(xCell.row() == 0);
	CHECK(twoCell.row() == 2);
}

TEST_CASE("CCsvTableModel: numbers are right-aligned", "[csv][model]")
{
	CCsvTableModel model;
	model.setTable(parse("12.5,text,,1 2"));

	const auto alignment = [&model](int column) { return model.index(0, column).data(Qt::TextAlignmentRole); };
	CHECK(alignment(0).toInt() == (Qt::AlignRight | Qt::AlignVCenter).toInt());
	CHECK_FALSE(alignment(1).isValid());
	CHECK_FALSE(alignment(2).isValid());
	CHECK_FALSE(alignment(3).isValid());
}

TEST_CASE("CCsvTableModel: comment rows", "[csv][model][comments]")
{
	CCsvTableModel model;

	SECTION("In file order, the text in the first column")
	{
		model.setTable(parse("#top\na,1\n#mid\nb,2", true));
		CHECK(model.rowCount() == 4);
		CHECK(model.columnCount() == 2);
		CHECK(commentRowsOf(model) == std::vector<int>{ 0, 2 });
		CHECK(model.index(0, 0).data(CCsvTableModel::CommentRowRole).toBool());
		CHECK_FALSE(model.index(1, 0).data(CCsvTableModel::CommentRowRole).toBool());
		CHECK(displayText(model, 0, 0) == "#top");
		CHECK(displayText(model, 0, 1).empty());
		CHECK(verticalHeadersOf(model) == Strings{ "1", "2", "3", "4" });
	}

	SECTION("The header is the first non-comment row")
	{
		CHECK(looksLikeHeader("# meta\n# more\nname,age\nbob,30", true));

		model.setTable(parse("# meta\n# more\nname,age\nbob,30", true));
		model.setFirstRowIsHeader(true);
		CHECK(horizontalHeaderOf(model, 0) == "name");
		CHECK(columnOf(model, 0) == Strings{ "# meta", "# more", "bob" });
		CHECK(commentRowsOf(model) == std::vector<int>{ 0, 1 });
		CHECK(verticalHeadersOf(model) == Strings{ "1", "2", "4" });
	}

	SECTION("Header detection samples data rows only")
	{
		CHECK(looksLikeHeader("name,score\n#note\nbob,7", true));
		// The 100th data row is the 150th row below the header
		CHECK_FALSE(looksLikeHeader("name,score\n" + repeated("x,1\n", 99) + repeated("#c\n", 50) + "x,oops\n", true));
	}

	SECTION("Nothing but comments")
	{
		model.setTable(parse("#a\n#b", true));
		CHECK_FALSE(model.firstRowLooksLikeHeader());
		model.setFirstRowIsHeader(true);
		CHECK(commentRowsOf(model) == std::vector<int>{ 0, 1 });
	}

	SECTION("Sorting hides them, clearing the sort restores them")
	{
		model.setTable(parse("#top\n3\n#mid\n1\n2", true));

		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "2", "3" });
		CHECK(commentRowsOf(model).empty());
		CHECK(verticalHeadersOf(model) == Strings{ "4", "5", "2" });

		model.sort(0, Qt::DescendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "3", "2", "1" });

		model.sort(-1, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "#top", "3", "#mid", "1", "2" });
		CHECK(commentRowsOf(model) == std::vector<int>{ 0, 2 });
		CHECK(verticalHeadersOf(model) == Strings{ "1", "2", "3", "4", "5" });
	}

	SECTION("Changing the header row while sorted restores them")
	{
		model.setTable(parse("#c\nval\n3\n1", true));
		model.setFirstRowIsHeader(true);
		model.sort(0, Qt::AscendingOrder);
		CHECK(columnOf(model, 0) == Strings{ "1", "3" });

		model.setFirstRowIsHeader(false);
		CHECK(columnOf(model, 0) == Strings{ "#c", "val", "3", "1" });
		CHECK(commentRowsOf(model) == std::vector<int>{ 0 });
	}

	SECTION("Only entering or leaving a sort resets the model, and only when there are comment rows")
	{
		int resets = 0;
		QObject::connect(&model, &QAbstractItemModel::modelReset, [&resets] { ++resets; });

		model.setTable(parse("#c\n3\n1", true));
		resets = 0;
		model.sort(0, Qt::AscendingOrder);
		CHECK(resets == 1);
		model.sort(0, Qt::DescendingOrder);
		CHECK(resets == 1);
		model.sort(-1, Qt::AscendingOrder);
		CHECK(resets == 2);
		model.sort(-1, Qt::AscendingOrder);
		CHECK(resets == 2);

		model.setTable(parse("3\n1", true));
		resets = 0;
		model.sort(0, Qt::AscendingOrder);
		model.sort(-1, Qt::AscendingOrder);
		CHECK(resets == 0);
	}

	SECTION("Persistent indexes follow their rows when re-sorting while sorted")
	{
		model.setTable(parse("#c\n3,x\n1,y\n2,z", true));
		model.sort(0, Qt::AscendingOrder);

		const QPersistentModelIndex xCell{ model.index(2, 1) };
		REQUIRE(xCell.data().toString().toStdString() == "x");

		model.sort(0, Qt::DescendingOrder);
		CHECK(xCell.row() == 0);
		CHECK(xCell.data().toString().toStdString() == "x");
	}
}

#include "ccsvparser.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

using Rows = std::vector<std::vector<std::string>>;

static CsvTable parse(const std::string& text, QChar delimiter = u',', bool skipCommentLines = false)
{
	return parseCsv(QString::fromStdString(text), delimiter, skipCommentLines);
}

// Each row's own cells, not padded to columnCount: ragged rows stay visible
static Rows rowsOf(const CsvTable& table)
{
	Rows rows;
	for (size_t row = 0; row < table.rowCount(); ++row)
	{
		auto& cells = rows.emplace_back();
		for (size_t column = 0; column < table.rowStarts[row + 1] - table.rowStarts[row]; ++column)
			cells.push_back(table.cell(row, column).toString().toStdString());
	}
	return rows;
}

static char delimiterOf(const std::string& text, bool skipCommentLines = false)
{
	return static_cast<char>(detectCsvDelimiter(QString::fromStdString(text), skipCommentLines).unicode());
}

static bool hasCommentLines(const std::string& text)
{
	return csvHasCommentLines(QString::fromStdString(text));
}

static std::string repeated(const std::string& line, int count)
{
	std::string result;
	for (int i = 0; i < count; ++i)
		result += line;
	return result;
}

TEST_CASE("parseCsv: empty and blank input", "[csv][parser]")
{
	const CsvTable empty = parse("");
	CHECK(empty.rowCount() == 0);
	CHECK(empty.columnCount == 0);

	CHECK(parse("\n\r\n\r\n\n").rowCount() == 0);

	// RFC 4180: spaces are part of a field, so a whitespace-only line is a row
	CHECK(rowsOf(parse("   ")) == Rows{ { "   " } });
}

TEST_CASE("parseCsv: line endings", "[csv][parser]")
{
	CHECK(rowsOf(parse("a\r\nb\nc\rd")) == Rows{ { "a" }, { "b" }, { "c" }, { "d" } });
	CHECK(rowsOf(parse("a,b")) == rowsOf(parse("a,b\r\n")));

	// LF then CR is two line breaks with a blank line between, not one
	CHECK(rowsOf(parse("a\n\rb")) == Rows{ { "a" }, { "b" } });
	CHECK(rowsOf(parse("a\r\r\nb")) == Rows{ { "a" }, { "b" } });
}

TEST_CASE("parseCsv: empty fields", "[csv][parser]")
{
	CHECK(rowsOf(parse(",")) == Rows{ { "", "" } });
	CHECK(parse(",").columnCount == 2);
	CHECK(rowsOf(parse("a,")) == Rows{ { "a", "" } });
	CHECK(rowsOf(parse("a,\nb")) == Rows{ { "a", "" }, { "b" } });
	CHECK(rowsOf(parse(R"("","")")) == Rows{ { "", "" } });

	// A quoted empty field is a value, unlike a blank line: the only way to write an empty single-column row
	CHECK(rowsOf(parse(R"("")")) == Rows{ { "" } });
	CHECK(rowsOf(parse("a\n\"\"\nb")) == Rows{ { "a" }, { "" }, { "b" } });
	CHECK(rowsOf(parse("\"\"\r\n\"\"\r\n")) == Rows{ { "" }, { "" } });
}

TEST_CASE("parseCsv: quoted fields", "[csv][parser]")
{
	CHECK(rowsOf(parse(R"("a,b",c)")) == Rows{ { "a,b", "c" } });
	CHECK(rowsOf(parse("\"line1\nline2\",x\ny")) == Rows{ { "line1\nline2", "x" }, { "y" } });
	CHECK(rowsOf(parse("\"crlf\r\ninside\"")) == Rows{ { "crlf\r\ninside" } });
	CHECK(rowsOf(parse("\"lone cr\rinside\"")) == Rows{ { "lone cr\rinside" } });

	CHECK(rowsOf(parse(R"("a""b")")) == Rows{ { "a\"b" } });
	CHECK(rowsOf(parse(R"("""")")) == Rows{ { "\"" } });
	CHECK(rowsOf(parse(R"("""""")")) == Rows{ { "\"\"" } });
	CHECK(rowsOf(parse(R"("a""","""b")")) == Rows{ { "a\"", "\"b" } });
	CHECK(rowsOf(parse(R"(""",""",x)")) == Rows{ { "\",\"", "x" } });
}

TEST_CASE("parseCsv: in-place unescaping keeps later cells intact", "[csv][parser]")
{
	// Every escape leaves the write cursor further behind the read cursor, across cells and rows
	const CsvTable table = parse("\"a\"\"b\",\"c\"\"\"\"d\",e\n\"f\"\"g\",h\ni,\"\"\"j\"\"\"");
	CHECK(rowsOf(table) == Rows{ { "a\"b", "c\"\"d", "e" }, { "f\"g", "h" }, { "i", "\"j\"" } });
}

TEST_CASE("parseCsv: malformed quoting keeps the text", "[csv][parser]")
{
	// A quote inside an unquoted field is a literal character
	CHECK(rowsOf(parse(R"(ab"c,d)")) == Rows{ { "ab\"c", "d" } });
	// A quote only opens a field as its very first character
	CHECK(rowsOf(parse(R"( "a,b")")) == Rows{ { " \"a", "b\"" } });

	// Text after the closing quote is appended
	CHECK(rowsOf(parse(R"("ab"cd,e)")) == Rows{ { "abcd", "e" } });
	CHECK(rowsOf(parse(R"("ab" ,c)")) == Rows{ { "ab ", "c" } });
	CHECK(rowsOf(parse(R"("a""" ,b)")) == Rows{ { "a\" ", "b" } });

	// An unterminated quote runs to the end of input, delimiters and line breaks included
	const CsvTable unterminated = parse("\"unterminated,x\ny,z");
	CHECK(rowsOf(unterminated) == Rows{ { "unterminated,x\ny,z" } });
	CHECK(unterminated.columnCount == 1);

	CHECK(rowsOf(parse("a,\"")) == Rows{ { "a", "" } });
}

TEST_CASE("parseCsv: ragged rows", "[csv][parser]")
{
	const CsvTable table = parse("a,b,c\nd\ne,f");
	CHECK(rowsOf(table) == Rows{ { "a", "b", "c" }, { "d" }, { "e", "f" } });
	CHECK(table.columnCount == 3);
	CHECK(table.cell(1, 1).isEmpty());
	CHECK(table.cell(1, 2).isEmpty());
	CHECK(table.cell(2, 2).isEmpty());
}

TEST_CASE("parseCsv: only the chosen delimiter splits", "[csv][parser]")
{
	CHECK(rowsOf(parse("a,b\tc;d", u'\t')) == Rows{ { "a,b", "c;d" } });
	CHECK(rowsOf(parse("\"x|y\"|z", u'|')) == Rows{ { "x|y", "z" } });
}

TEST_CASE("parseCsv: comment lines", "[csv][parser][comments]")
{
	SECTION("Skipped lines do not form rows or widen the table")
	{
		const CsvTable table = parse("#c1\na,b\n# c2, with, delimiters\nc,d\n#last", u',', true);
		CHECK(rowsOf(table) == Rows{ { "a", "b" }, { "c", "d" } });
		CHECK(table.commentLineCount == 3);
		CHECK(table.columnCount == 2);
	}

	SECTION("Every line break style ends a comment")
	{
		const CsvTable table = parse("#crlf\r\na\r\n#lf\nb\n#cr\rc", u',', true);
		CHECK(rowsOf(table) == Rows{ { "a" }, { "b" }, { "c" } });
		CHECK(table.commentLineCount == 3);
	}

	SECTION("Not skipped when disabled")
	{
		const CsvTable table = parse("#x,y", u',', false);
		CHECK(rowsOf(table) == Rows{ { "#x", "y" } });
		CHECK(table.commentLineCount == 0);
	}

	SECTION("Only a # as the first character of a row starts a comment")
	{
		CHECK(rowsOf(parse("a,#b", u',', true)) == Rows{ { "a", "#b" } });
		CHECK(rowsOf(parse(" #b", u',', true)) == Rows{ { " #b" } });
		CHECK(rowsOf(parse("\"#a\",b", u',', true)) == Rows{ { "#a", "b" } });
	}

	SECTION("A quote in a comment does not open a field")
	{
		const CsvTable table = parse("# say \"hi\na,b", u',', true);
		CHECK(rowsOf(table) == Rows{ { "a", "b" } });
		CHECK(table.commentLineCount == 1);
	}

	SECTION("A # line inside a quoted field is data")
	{
		const CsvTable table = parse("\"x\n#y\",z\n#c", u',', true);
		CHECK(rowsOf(table) == Rows{ { "x\n#y", "z" } });
		CHECK(table.commentLineCount == 1);
	}

	SECTION("A file of only comments and blank lines")
	{
		const CsvTable table = parse("#\n\n#,,,\r\n", u',', true);
		CHECK(table.rowCount() == 0);
		CHECK(table.columnCount == 0);
		CHECK(table.commentLineCount == 2);
	}
}

TEST_CASE("detectCsvDelimiter", "[csv][delimiter]")
{
	SECTION("Comma when no candidate appears")
	{
		CHECK(delimiterOf("") == ',');
		CHECK(delimiterOf("abc\ndef") == ',');
	}

	SECTION("A consistent candidate beats a more frequent inconsistent one")
	{
		// Decimal commas vary per line, the semicolon count does not
		CHECK(delimiterOf("1,5;2,25\n3;4,125") == ';');
	}

	SECTION("A line lacking the candidate counts as zero, not as absent")
	{
		CHECK(delimiterOf("a;b\nc;d,e,f,g\nh;i,j,k,l") == ';');
	}

	SECTION("Among consistent candidates the most frequent wins")
	{
		CHECK(delimiterOf("a;b;c|d\ne;f;g|h") == ';');
	}

	SECTION("A tie goes to the earlier of comma, semicolon, tab, pipe")
	{
		CHECK(delimiterOf("a,b;c\nd,e;f") == ',');
		CHECK(delimiterOf("a|b;c\nd|e;f") == ';');
		CHECK(delimiterOf("a|b\tc\nd|e\tf") == '\t');
	}

	SECTION("Blank and whitespace-only lines do not break consistency")
	{
		// Counted, the blank line would make the semicolon inconsistent and the more frequent comma win
		CHECK(delimiterOf("a,b,c;d\ne;f\n\ng,h,i,j;k") == ';');
		CHECK(delimiterOf("a,b,c;d\r\ne;f\r\n \t \r\ng,h,i,j;k") == ';');
	}

	SECTION("Only the first 20 non-blank lines are sampled")
	{
		const std::string breaker = "x;y" + std::string(50, ',') + "\n";
		CHECK(delimiterOf(repeated("a,b;c;d\n", 20) + breaker) == ';');
		CHECK(delimiterOf(repeated("a,b;c;d\n", 19) + breaker) == ',');
		// Blank lines take no sample slots: the breaker is the 11th sampled line, the 21st physical one
		CHECK(delimiterOf(repeated("a,b;c;d\n\n", 10) + breaker) == ',');
	}

	SECTION("CR-only line endings")
	{
		CHECK(delimiterOf("a;b,c,d,e\rf;g\rh;i") == ';');
	}

	SECTION("Comment lines are ignored only when skipped")
	{
		const std::string text = "# a, b, c, d\na;b\nc;d";
		CHECK(delimiterOf(text, true) == ';');
		CHECK(delimiterOf(text, false) == ',');
	}
}

TEST_CASE("csvHasCommentLines", "[csv][comments]")
{
	SECTION("No # at all")
	{
		CHECK_FALSE(hasCommentLines(""));
		CHECK_FALSE(hasCommentLines("a,b\nc,d"));
	}

	SECTION("A leading block, regardless of its size")
	{
		CHECK(hasCommentLines("# a\n# b\nx,y\n1,2"));
		CHECK(hasCommentLines(repeated("# meta\n", 50) + "x,y"));
		CHECK(hasCommentLines("\n# a\n\n# b\n\nx,y"));
	}

	SECTION("No data lines at all: a column of # values, not comments")
	{
		CHECK_FALSE(hasCommentLines("#a\n#b"));
		CHECK_FALSE(hasCommentLines("#ff0000,red\n#00ff00,green"));
	}

	SECTION("A # anywhere in a data line means # is data")
	{
		CHECK_FALSE(hasCommentLines("# meta\nid,tag\n1,#red"));
		CHECK_FALSE(hasCommentLines("# meta\na,\"b#\""));
		CHECK_FALSE(hasCommentLines("a#\n#c"));
		// Indented, it is a data line containing #
		CHECK_FALSE(hasCommentLines("  # a\nx"));
	}

	SECTION("The whole file is checked, not a sample")
	{
		CHECK_FALSE(hasCommentLines("# meta\n" + repeated("1,2\n", 10000) + "3,#4\n"));
	}

	SECTION("Scattered comments: at most one per ten data lines")
	{
		CHECK(hasCommentLines("x\n#c\n" + repeated("x\n", 9)));
		CHECK_FALSE(hasCommentLines("x\n#c\n" + repeated("x\n", 8)));
		CHECK(hasCommentLines("x\n#c\n#c\n" + repeated("x\n", 19)));
		CHECK_FALSE(hasCommentLines("x\n#c\n#c\n" + repeated("x\n", 18)));
	}

	SECTION("A trailing block is scattered, not leading")
	{
		CHECK_FALSE(hasCommentLines("x,y\n#end"));
		CHECK(hasCommentLines(repeated("x,y\n", 10) + "#end"));
	}

	SECTION("Leading block plus scattered comments: the ratio applies to all of them")
	{
		CHECK(hasCommentLines("#a\n#b\nx\n#c\n" + repeated("x\n", 29)));
		CHECK_FALSE(hasCommentLines("#a\n#b\nx\n#c\n" + repeated("x\n", 28)));
	}

	SECTION("Line break styles")
	{
		CHECK(hasCommentLines("#a\r\nx\r\n"));
		CHECK(hasCommentLines("#a\rx,y"));
	}
}

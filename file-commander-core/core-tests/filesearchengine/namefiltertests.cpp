#include "searchenginetesthelpers.h"

TEST_CASE("Search - a name filter matches the whole name, not a substring of it", "[search][names]")
{
	TempTree tree;
	const QString exact = tree.makeFile(QSL("notes.txt"));
	const QString longerName = tree.makeFile(QSL("mynotes.txt"));
	const QString otherExtension = tree.makeFile(QSL("notes.md"));

	const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("notes.txt") } });

	CHECK(result.matched(exact));
	// Anchored at both ends: wrapping a query in '*' for a partial match is the dialog's job, not the engine's.
	CHECK_FALSE(result.matched(longerName));
	CHECK_FALSE(result.matched(otherExtension));
}

TEST_CASE("Search - '*' and '?' are the wildcards", "[search][names]")
{
	TempTree tree;
	const QString oneDigit = tree.makeFile(QSL("log1.txt"));
	const QString twoDigits = tree.makeFile(QSL("log10.txt"));
	const QString otherExtension = tree.makeFile(QSL("log1.md"));

	SECTION("'?' stands for exactly one character")
	{
		const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("log?.txt") } });
		CHECK(result.matched(oneDigit));
		CHECK_FALSE(result.matched(twoDigits));
	}

	SECTION("'*' stands for any run of characters, including none")
	{
		const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("log*.txt") } });
		CHECK(result.matched(oneDigit));
		CHECK(result.matched(twoDigits));
		CHECK_FALSE(result.matched(otherExtension));
	}
}

TEST_CASE("Search - every filter in the list contributes its matches", "[search][names]")
{
	TempTree tree;
	const QString text = tree.makeFile(QSL("a.txt"));
	const QString document = tree.makeFile(QSL("b.doc"));
	const QString image = tree.makeFile(QSL("c.png"));

	const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("*.txt"), QSL("*.doc") } });

	CHECK(result.matched(text));
	CHECK(result.matched(document));
	CHECK_FALSE(result.matched(image));
}

TEST_CASE("Search - name case sensitivity follows the flag", "[search][names]")
{
	TempTree tree;
	const QString upperCaseName = tree.makeFile(QSL("README.TXT"));

	CHECK(runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("readme.txt") } }).matched(upperCaseName));
	CHECK_FALSE(runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("readme.txt") }, .nameCaseSensitive = true }).matched(upperCaseName));
}

TEST_CASE("Search - the whole subtree under every given root is searched", "[search][names]")
{
	TempTree tree;
	const QString shallow = tree.makeFile(QSL("left/target.txt"));
	const QString deep = tree.makeFile(QSL("left/one/two/target.txt"));
	const QString otherRoot = tree.makeFile(QSL("right/target.txt"));

	SECTION("one root, recursively")
	{
		const SearchResult result = runSearch({ .roots = { tree.path(QSL("left")) }, .nameFilters = { QSL("target.txt") } });
		CHECK(result.matched(shallow));
		CHECK(result.matched(deep));
		CHECK_FALSE(result.matched(otherRoot));
	}

	SECTION("several roots")
	{
		const SearchResult result = runSearch({ .roots = { tree.path(QSL("left")), tree.path(QSL("right")) }, .nameFilters = { QSL("target.txt") } });
		CHECK(result.matched(shallow));
		CHECK(result.matched(deep));
		CHECK(result.matched(otherRoot));
	}
}

TEST_CASE("Search - a name search reports directories as well as files", "[search][names]")
{
	TempTree tree;
	const QString directory = tree.makeDir(QSL("archive"));
	const QString file = tree.makeFile(QSL("archive.txt"));

	const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("archive*") } });

	CHECK(result.matched(directory));
	CHECK(result.matched(file));
}

// Segment-B finding: queryToRegex() escapes only '.', so every other regex metacharacter in a file name is
// interpreted instead of matched. Drop [!shouldfail] once name filters are translated as wildcards.
TEST_CASE("Search - regex metacharacters in a name filter are matched literally", "[search][names][!shouldfail]")
{
	TempTree tree;
	const QString parentheses = tree.makeFile(QSL("report (final).doc"));
	const QString brackets = tree.makeFile(QSL("notes[1].txt"));
	const QString plus = tree.makeFile(QSL("a+b.log"));
	// The names those filters match today instead of the ones above.
	const QString bracketDecoy = tree.makeFile(QSL("notes1.txt"));
	const QString plusDecoy = tree.makeFile(QSL("aaab.log"));

	CHECK(runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("report (final).doc") } }).matched(parentheses));

	const SearchResult bracketSearch = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("notes[1].txt") } });
	CHECK(bracketSearch.matched(brackets));
	CHECK_FALSE(bracketSearch.matched(bracketDecoy));

	const SearchResult plusSearch = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("a+b.log") } });
	CHECK(plusSearch.matched(plus));
	CHECK_FALSE(plusSearch.matched(plusDecoy));
}

// Segment-B finding: an unbalanced bracket yields an invalid QRegularExpression, which assert_r only logs. The
// search then runs a pattern that can never match and reports success with nothing found.
TEST_CASE("Search - a name filter that would be an invalid regex still finds its file", "[search][names][!shouldfail]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("a[1.txt"));

	CHECK(runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("a[1.txt") } }).matched(file));
}

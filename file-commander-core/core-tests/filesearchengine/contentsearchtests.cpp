#include "searchenginetesthelpers.h"

TEST_CASE("Search - plain content matching finds the text and nothing else", "[search][contents]")
{
	TempTree tree;
	const QString hit = tree.makeFile(QSL("hit.txt"), "the needle is here");
	const QString miss = tree.makeFile(QSL("miss.txt"), "nothing of interest");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .contents = QSL("needle"), .contentsCaseSensitive = true });

	CHECK(result.matched(hit));
	CHECK_FALSE(result.matched(miss));
}

TEST_CASE("Search - content case sensitivity follows the flag", "[search][contents]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("f.txt"), "Needle");

	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("needle") }).matched(file));
	CHECK_FALSE(runSearch({ .roots = { tree.path() }, .contents = QSL("needle"), .contentsCaseSensitive = true }).matched(file));
}

TEST_CASE("Search - a file has to pass both the name filter and the content filter", "[search][contents]")
{
	TempTree tree;
	const QString both = tree.makeFile(QSL("match.txt"), "needle");
	const QString wrongExtension = tree.makeFile(QSL("match.md"), "needle");
	const QString wrongContents = tree.makeFile(QSL("other.txt"), "haystack");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = { QSL("*.txt") },
		.contents = QSL("needle"), .contentsCaseSensitive = true });

	CHECK(result.matched(both));
	CHECK_FALSE(result.matched(wrongExtension));
	CHECK_FALSE(result.matched(wrongContents));
}

TEST_CASE("Search - contents alone are a complete query, with no name filter at all", "[search][contents]")
{
	TempTree tree;
	const QString hit = tree.makeFile(QSL("hit.dat"), "needle");
	const QString miss = tree.makeFile(QSL("miss.dat"), "haystack");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .nameFilters = {},
		.contents = QSL("needle"), .contentsCaseSensitive = true });

	CHECK(result.matched(hit));
	CHECK_FALSE(result.matched(miss));
}

TEST_CASE("Search - a content search never reports a directory", "[search][contents]")
{
	TempTree tree;
	tree.makeDir(QSL("needle")); // A directory whose name is the text being looked for
	const QString file = tree.makeFile(QSL("f.txt"), "needle");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .contents = QSL("needle"), .contentsCaseSensitive = true });

	CHECK(result.count() == 1);
	CHECK(result.matched(file));
}

TEST_CASE("Search - a file with nothing to match in it is never reported", "[search][contents]")
{
	TempTree tree;
	tree.makeFile(QSL("empty.txt"));
	tree.makeFile(QSL("shorter-than-the-needle.txt"), "ab");

	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("needle"), .contentsCaseSensitive = true }).count() == 0);
}

TEST_CASE("Search - NUL bytes do not hide the text around them", "[search][contents]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("binary.dat"), QByteArray("head\0\0\0needle", 13));

	// Case-insensitivity routes this through the regex path, the one that has to substitute the NULs out first.
	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("needle") }).matched(file));
}

TEST_CASE("Search - text is found wherever it sits within a scan window", "[search][contents]")
{
	TempTree tree;
	const QByteArray needle = "NEEDLE";
	const qsizetype fileSize = contentScanWindowSize * 2;
	const QString atStart = tree.makeFileWithNeedleAt(QSL("start.txt"), needle, 0, fileSize);
	const QString endingOnBoundary = tree.makeFileWithNeedleAt(QSL("before.txt"), needle, contentScanWindowSize - needle.size(), fileSize);
	const QString startingOnBoundary = tree.makeFileWithNeedleAt(QSL("after.txt"), needle, contentScanWindowSize, fileSize);

	const SearchResult result = runSearch({ .roots = { tree.path() }, .contents = QSL("NEEDLE"), .contentsCaseSensitive = true });

	CHECK(result.matched(atStart));
	CHECK(result.matched(endingOnBoundary));
	CHECK(result.matched(startingOnBoundary));
}

// Segment-B finding: fileContentsMatches() scans disjoint 4 KiB windows, so text spanning the boundary between
// two of them is in neither. Drop [!shouldfail] once consecutive windows overlap.
// The two match paths need separate fixes, hence separate cases.
TEST_CASE("Search - plain text straddling a scan window boundary is found", "[search][contents][!shouldfail]")
{
	TempTree tree;
	const QByteArray needle = "NEEDLE";
	const QString file = tree.makeFileWithNeedleAt(QSL("straddle.txt"), needle, contentScanWindowSize - 3, contentScanWindowSize * 2);

	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("NEEDLE"), .contentsCaseSensitive = true }).matched(file));
}

TEST_CASE("Search - a regex match straddling a scan window boundary is found", "[search][contents][!shouldfail]")
{
	TempTree tree;
	const QByteArray needle = "NEEDLE";
	const QString file = tree.makeFileWithNeedleAt(QSL("straddle.txt"), needle, contentScanWindowSize - 3, contentScanWindowSize * 2);

	// Case-insensitivity routes this through the regex path rather than through memfind.
	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("needle") }).matched(file));
}

TEST_CASE("Search - whole-word matching excludes text inside a longer word", "[search][contents]")
{
	TempTree tree;
	const QString insideAWord = tree.makeFile(QSL("inside.txt"), "concatenate");
	const QString standalone = tree.makeFile(QSL("standalone.txt"), "the cat sat");

	const SearchResult result = runSearch({ .roots = { tree.path() },
		.contents = QSL("cat"), .contentsCaseSensitive = true, .contentsWholeWords = true });

	CHECK_FALSE(result.matched(insideAWord));
	CHECK(result.matched(standalone));
}

TEST_CASE("Search - a content regex is used as written", "[search][contents]")
{
	TempTree tree;
	const QString optionalLetter = tree.makeFile(QSL("optional.txt"), "the color red");
	const QString escapedDot = tree.makeFile(QSL("version.txt"), "version 1.2");
	const QString wildcardDecoy = tree.makeFile(QSL("decoy.txt"), "the colouXr red");

	const SearchResult optionalSearch = runSearch({ .roots = { tree.path() },
		.contents = QSL("colou?r"), .contentsCaseSensitive = true, .contentsIsRegex = true });
	CHECK(optionalSearch.matched(optionalLetter));
	CHECK_FALSE(optionalSearch.matched(wildcardDecoy)); // What would match if '?' were translated as a wildcard

	CHECK(runSearch({ .roots = { tree.path() },
		.contents = QSL("[0-9]\\.[0-9]"), .contentsCaseSensitive = true, .contentsIsRegex = true }).matched(escapedDot));
}

// Case-insensitivity routes a plain-text query through the regex engine, where it has to survive as literal text.
TEST_CASE("Search - plain text is matched literally even when case-insensitive", "[search][contents]")
{
	TempTree tree;
	const QString literal = tree.makeFile(QSL("literal.txt"), "a*b");
	const QString wildcardDecoy = tree.makeFile(QSL("decoy.txt"), "axxxb");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .contents = QSL("a*b") });

	CHECK(result.matched(literal));
	CHECK_FALSE(result.matched(wildcardDecoy));
}

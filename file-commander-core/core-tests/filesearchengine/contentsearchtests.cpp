#include "searchenginetesthelpers.h"

// U+00E9 and U+00EF as UTF-8, spelled in bytes: these tests turn on which side of a scan window boundary each
// individual byte lands on, so the encoding must be the test's own statement and not the source file's.
static const QByteArray eAcuteUtf8("\xC3\xA9", 2);
static const QByteArray iDiaeresisUtf8("\xC3\xAF", 2);

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

// The regex engine is handed one scan window at a time and is told nothing about where the file begins or ends,
// so file-wide anchoring over-reports - the behavior doc/search.md describes.
TEST_CASE("Search - ^ and $ anchor to the scan window, not to the file", "[search][contents]")
{
	TempTree tree;
	const QByteArray needle = "NEEDLE";
	const qsizetype fileSize = contentScanWindowSize * 2;
	const QString atSecondWindowStart = tree.makeFileWithNeedleAt(QSL("second-window.txt"), needle, contentScanWindowSize, fileSize);
	const QString atFirstWindowEnd = tree.makeFileWithNeedleAt(QSL("first-window.txt"), needle, contentScanWindowSize - needle.size(), fileSize);
	const QString midWindow = tree.makeFileWithNeedleAt(QSL("mid-window.txt"), needle, 100, fileSize);

	const SearchResult anchoredToStart = runSearch({ .roots = { tree.path() },
		.contents = QSL("^NEEDLE"), .contentsCaseSensitive = true, .contentsIsRegex = true });
	CHECK(anchoredToStart.matched(atSecondWindowStart)); // Nowhere near the start of the file
	CHECK_FALSE(anchoredToStart.matched(midWindow)); // '^' does still anchor - just to the window

	const SearchResult anchoredToEnd = runSearch({ .roots = { tree.path() },
		.contents = QSL("NEEDLE$"), .contentsCaseSensitive = true, .contentsIsRegex = true });
	CHECK(anchoredToEnd.matched(atFirstWindowEnd));
	CHECK_FALSE(anchoredToEnd.matched(midWindow));
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

// The same window-edge effect as the anchors above: the '\b' the whole-word option adds sees where the window
// stops, not where the word does. Space padding is what puts a real boundary before the word.
TEST_CASE("Search - whole-word matching treats a scan window edge as a word boundary", "[search][contents]")
{
	TempTree tree;
	const QByteArray word = "concatenate";
	const QByteArray queryPart = "concat"; // Positioned below to end exactly at the window edge
	const qsizetype fileSize = contentScanWindowSize * 2;
	const QString splitWord = tree.makeFileWithNeedleAt(QSL("split.txt"), word, contentScanWindowSize - queryPart.size(), fileSize, ' ');
	const QString wholeWord = tree.makeFileWithNeedleAt(QSL("whole.txt"), word, 100, fileSize, ' ');

	const SearchResult result = runSearch({ .roots = { tree.path() },
		.contents = QString::fromUtf8(queryPart), .contentsCaseSensitive = true, .contentsWholeWords = true });

	CHECK_FALSE(result.matched(wholeWord)); // Inside a longer word
	CHECK(result.matched(splitWord)); // The very same word, reported only because "concat" ends the window
}

// '\b' and '\w' are ASCII-only unless the pattern carries UseUnicodePropertiesOption, which nothing sets. A
// non-ASCII letter therefore reads as a non-word character, in both directions.
TEST_CASE("Search - whole-word matching is Unicode-aware", "[search][contents][!shouldfail]")
{
	TempTree tree;
	const QString insideAWord = tree.makeFile(QSL("inside.txt"), "na" + iDiaeresisUtf8 + "ve");
	const QString standalone = tree.makeFile(QSL("standalone.txt"), "caf" + eAcuteUtf8 + " au lait");

	CHECK_FALSE(runSearch({ .roots = { tree.path() }, .contents = QSL("na"),
		.contentsCaseSensitive = true, .contentsWholeWords = true }).matched(insideAWord));

	CHECK(runSearch({ .roots = { tree.path() }, .contents = QString::fromUtf8("caf" + eAcuteUtf8),
		.contentsCaseSensitive = true, .contentsWholeWords = true }).matched(standalone));
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

TEST_CASE("Search - non-ASCII text is found on both match paths", "[search][contents]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("accented.txt"), "caf" + eAcuteUtf8 + " au lait");
	const QString needle = QString::fromUtf8("caf" + eAcuteUtf8);

	CHECK(runSearch({ .roots = { tree.path() }, .contents = needle, .contentsCaseSensitive = true }).matched(file)); // memfind

	// Case-insensitivity routes the same query through the regex engine, where the non-ASCII letter has to come
	// out of QRegularExpression::escape() as a valid pattern rather than as a broken escape.
	const SearchResult viaRegex = runSearch({ .roots = { tree.path() }, .contents = needle });
	CHECK(viaRegex.status == CFileSearchEngine::SearchFinished);
	CHECK(viaRegex.matched(file));
}

TEST_CASE("Search - a character split across a scan window boundary does not hide the text after it", "[search][contents]")
{
	TempTree tree;
	const QByteArray needle = "NEEDLE";
	QByteArray contents(contentScanWindowSize * 2, TempTree::paddingByte);
	contents.replace(contentScanWindowSize - 1, eAcuteUtf8.size(), eAcuteUtf8); // One byte in each window
	contents.replace(contentScanWindowSize + 8, needle.size(), needle); // Wholly inside the second window
	const QString file = tree.makeFile(QSL("split-char.txt"), contents);

	// Both halves decode to a replacement character, but the decoder resynchronizes at the next lead byte, so
	// what the window loses is that one character and not everything after it.
	CHECK(runSearch({ .roots = { tree.path() }, .contents = QSL("needle") }).matched(file));
}

// The straddle cases above split ASCII text between two windows; this one splits a single character, so an
// overlapping-window fix has to hand the decoder whole characters and not merely more bytes.
TEST_CASE("Search - non-ASCII text straddling a scan window boundary is found", "[search][contents][!shouldfail]")
{
	TempTree tree;
	const QByteArray cafe = "caf" + eAcuteUtf8;
	const QString file = tree.makeFileWithNeedleAt(QSL("straddle-char.txt"), cafe, contentScanWindowSize - 4, contentScanWindowSize * 2);

	CHECK(runSearch({ .roots = { tree.path() }, .contents = QString::fromUtf8(cafe) }).matched(file));
}

#include "searchenginetesthelpers.h"

#include <algorithm>
#include <semaphore>

TEST_CASE("Search - a query with nowhere to look is refused", "[search][engine]")
{
	SearchRunner runner;

	// The roots are the one part of a query with no sensible default; everything else may be left out.
	CHECK_FALSE(runner.start({ .roots = {}, .nameFilters = { QSL("*.txt") } }));
}

TEST_CASE("Search - a content regex that does not compile is reported as such, not as a cancellation", "[search][engine]")
{
	TempTree tree;
	tree.makeFile(QSL("f.txt"), "anything at all");

	const SearchResult result = runSearch({ .roots = { tree.path() },
		.contents = QSL("["), .contentsCaseSensitive = true, .contentsIsRegex = true });

	// A status of its own is what lets the dialog say why nothing was found, rather than "search aborted".
	CHECK(result.status == CFileSearchEngine::SearchInvalidPattern);
	CHECK(result.count() == 0);
	CHECK(result.itemsScanned == 0);
	CHECK(result.finishedNotifications == 1);
}

TEST_CASE("Search - a second search is refused while one is already running", "[search][engine]")
{
	TempTree tree;
	tree.makeFile(QSL("a.txt"));

	SearchRunner runner;
	// Holding the search on the first item it reports is what makes the second attempt land while the first is
	// definitely still running, rather than racing its completion.
	std::binary_semaphore mayProceed{ 0 };
	runner.setItemScannedHook([&mayProceed] { mayProceed.acquire(); });

	REQUIRE(runner.start({ .roots = { tree.path() } }));
	CHECK_FALSE(runner.start({ .roots = { tree.path() } })); // Refused outright: it must not stop the running one
	CHECK(runner.searchInProgress());

	mayProceed.release();
	CHECK(runner.finish().status == CFileSearchEngine::SearchFinished);
}

TEST_CASE("Search - every traversed entry is counted, directories included", "[search][engine]")
{
	TempTree tree;
	tree.makeFile(QSL("a.txt"));
	tree.makeFile(QSL("sub/b.txt"));

	const SearchResult result = runSearch({ .roots = { tree.path() } });

	CHECK(result.itemsScanned == 3); // The root's two entries (a.txt and sub), and sub's one entry; the root itself is not an item
	CHECK(result.status == CFileSearchEngine::SearchFinished);
	CHECK(result.finishedNotifications == 1);
}

TEST_CASE("Search - nothing in a tree without links is flagged as reached through one", "[search][engine]")
{
	TempTree tree;
	tree.makeFile(QSL("a.txt"));
	tree.makeFile(QSL("sub/b.txt"));

	const SearchResult result = runSearch({ .roots = { tree.path() } });

	REQUIRE(result.count() > 0); // Otherwise the check below passes for the wrong reason
	CHECK(result.linkReachedMatches == 0);
}

TEST_CASE("Search - a root listed twice is scanned once", "[search][engine]")
{
	TempTree tree;
	tree.makeFile(QSL("a.txt"));

	CHECK(runSearch({ .roots = { tree.path(), tree.path(), QString{ tree.path() % '/' } } }).count() == 1);
#ifdef _WIN32
	CHECK(runSearch({ .roots = { tree.path(), tree.path().toUpper() } }).count() == 1);
#endif
}

#ifdef _WIN32
TEST_CASE("Search - roots whose names only Unicode case folding equates are different folders", "[search][engine]")
{
	TempTree tree;
	const QString kelvinSignName = QString{ QChar{ 0x212A } } + u'x'; // Folds to "kx"
	const QString first = tree.makeDir(kelvinSignName), second = tree.makeDir(QSL("kx"));
	if (QDir{ tree.path() }.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() != 2)
		return; // This volume's case table equates the two names

	const QString firstFile = tree.makeFile(kelvinSignName + QSL("/a.txt")), secondFile = tree.makeFile(QSL("kx/b.txt"));
	const SearchResult result = runSearch({ .roots = { first, second } });
	CHECK(result.matched(firstFile));
	CHECK(result.matched(secondFile));
	CHECK(result.count() == 2);
}
#endif

TEST_CASE("Search - overlapping roots report each item once, in either order", "[search][engine]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QSL("sub"));
	const QString nested = tree.makeDir(QSL("sub/nested"));
	const QString file = tree.makeFile(QSL("sub/nested/a.txt"));

	for (const QStringList& roots : { QStringList{ tree.path(), nested }, QStringList{ nested, tree.path() }, QStringList{ sub, nested, tree.path() } })
	{
		INFO(roots.join(u';').toStdString());
		const SearchResult result = runSearch({ .roots = roots });
		CHECK(result.matched(sub));
		CHECK(result.matched(nested));
		CHECK(result.matched(file));
		CHECK(result.count() == 3);
		CHECK(result.itemsScanned == 3);
	}
}

TEST_CASE("Search - a root inside an excluded folder is searched, though the higher root's scan skips that folder", "[search][engine]")
{
	TempTree tree;
	const QString excluded = tree.makeDir(QSL("build"));
	const QString inExcluded = tree.makeFile(QSL("build/a.txt"));
	const QString outside = tree.makeFile(QSL("b.txt"));

	const SearchResult result = runSearch({ .roots = { tree.path(), excluded }, .excludedFolders = excludedFolderFiltersFromSearchField(QSL("build")) });
	CHECK(result.matched(inExcluded));
	CHECK(result.matched(outside));
	CHECK(result.count() == 2);
}

#ifndef _WIN32
TEST_CASE("Search - a root below a folder that can be entered but not listed is searched", "[search][engine]")
{
	TempTree tree;
	const QString locked = tree.makeDir(QSL("locked"));
	const QString reachable = tree.makeDir(QSL("locked/reachable"));
	const QString file = tree.makeFile(QSL("locked/reachable/a.txt"));

	// FAT and exFAT refuse the change and leave the folder listable, and the search must find the file either way
	const QFile::Permissions originalPermissions = QFile::permissions(locked);
	QFile::setPermissions(locked, QFile::ExeOwner);
	const SearchResult result = runSearch({ .roots = { tree.path(), reachable } });
	QFile::setPermissions(locked, originalPermissions); // The temporary tree cannot be removed without them

	CHECK(std::ranges::count(result.matches, file) == 1);
}
#endif

TEST_CASE("Search - cancelling stops the traversal and reports the search as cancelled", "[search][engine]")
{
	TempTree tree;
	constexpr int fileCount = 50;
	for (int i = 0; i < fileCount; ++i)
		tree.makeFile(QSL("f%1.txt").arg(i));

	SearchRunner runner;
	// Deterministic rather than timed: the engine reports the very first item it scans, and it does so on the
	// search thread, so cancellation is in place before the traversal can reach anything else.
	runner.setItemScannedHook([&runner] { runner.stop(); });

	const SearchResult result = runner.run({ .roots = { tree.path() } });

	CHECK(result.status == CFileSearchEngine::SearchCancelled);
	CHECK(result.itemsScanned < fileCount);
	CHECK(result.finishedNotifications == 1);
}

TEST_CASE("Search - the engine is reusable once a search has finished", "[search][engine]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("a.txt"));
	const FileSearchQuery query{ .roots = { tree.path() }, .nameFilters = { QSL("*.txt") } };

	SearchRunner runner;
	CHECK(runner.run(query).matched(file));
	CHECK_FALSE(runner.searchInProgress());

	runner.clearRecorded();
	const SearchResult second = runner.run(query);

	CHECK(second.matched(file));
	CHECK(second.finishedNotifications == 1);
}

TEST_CASE("Search - a content search spread across the worker pool reports every match", "[search][engine]")
{
	TempTree tree;
	// Comfortably more files than the pool has task slots, so the traversal has to wait for capacity and the
	// drain at the end has to collect what is still outstanding.
	constexpr int fileCount = 200;
	for (int i = 0; i < fileCount; ++i)
		tree.makeFile(QSL("f%1.txt").arg(i), "needle");

	const SearchResult result = runSearch({ .roots = { tree.path() }, .contents = QSL("needle"), .contentsCaseSensitive = true });

	CHECK(result.count() == fileCount);
	CHECK(result.status == CFileSearchEngine::SearchFinished);
}

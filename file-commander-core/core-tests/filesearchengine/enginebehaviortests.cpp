#include "searchenginetesthelpers.h"

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

	CHECK(result.itemsScanned == 4); // The root, its two entries (a.txt and sub), and sub's one entry
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
	const SearchQuery query{ .roots = { tree.path() }, .nameFilters = { QSL("*.txt") } };

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

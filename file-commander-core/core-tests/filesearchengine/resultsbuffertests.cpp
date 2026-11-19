#include "searchenginetesthelpers.h"

#include "filesearchengine/cfilesearchresultsbuffer.h"

#include <stddef.h>

TEST_CASE("Results buffer - matches come out in batches, oldest first, and the completion with the last of them", "[search][buffer]")
{
	CFileSearchResultsBuffer buffer;

	TempTree tree;
	const QStringList paths{ tree.makeFile(QSL("a")), tree.makeFile(QSL("b")), tree.makeFile(QSL("c")), tree.makeFile(QSL("d")), tree.makeFile(QSL("e")) };
	for (const QString& path : paths)
		buffer.matchFound(CFileSystemObject{ path }, path == paths[1]);
	buffer.searchFinished(CFileSearchEngine::SearchCancelled, 7, 2, 30);

	CFileSearchResultsBuffer::Batch batch = buffer.take(2);
	REQUIRE(batch.matches.size() == 2);
	CHECK(batch.matches[0].item.fullAbsolutePath() == paths[0]);
	CHECK_FALSE(batch.matches[0].reachedThroughLink);
	CHECK(batch.matches[1].item.fullAbsolutePath() == paths[1]);
	CHECK(batch.matches[1].reachedThroughLink);
	CHECK_FALSE(batch.completion.has_value());

	batch = buffer.take(2);
	CHECK(batch.matches.size() == 2);
	CHECK_FALSE(batch.completion.has_value());

	batch = buffer.take(2);
	REQUIRE(batch.matches.size() == 1);
	CHECK(batch.matches[0].item.fullAbsolutePath() == paths[4]);
	REQUIRE(batch.completion.has_value());
	CHECK(batch.completion->status == CFileSearchEngine::SearchCancelled);
	CHECK(batch.completion->itemsScanned == 7);
	CHECK(batch.completion->inconclusiveItems == 2);
	CHECK(batch.completion->msElapsed == 30);

	// Handed out once
	CHECK_FALSE(buffer.take(2).completion.has_value());
}

TEST_CASE("Results buffer - only the last scanned item is kept, and handed out once", "[search][buffer]")
{
	TempTree tree;
	const QString file = tree.makeFile(QSL("a"));
	CFileSearchResultsBuffer buffer;

	buffer.itemScanned(QSL("first"));
	buffer.itemScanned(QSL("second"));
	CHECK(buffer.take(10).lastScannedItem == QSL("second"));

	buffer.matchFound(CFileSystemObject{ file }, false);
	const CFileSearchResultsBuffer::Batch batch = buffer.take(10);
	CHECK(batch.lastScannedItem.isEmpty());
	CHECK(batch.matches.size() == 1);
}

TEST_CASE("Results buffer - a search's matches all arrive before its completion", "[search][buffer]")
{
	TempTree tree;
	static constexpr size_t fileCount = 50;
	for (size_t i = 0; i < fileCount; ++i)
		tree.makeFile(QSL("dir%1/file%1.txt").arg(i), "needle");

	CFileSearchResultsBuffer buffer;
	CFileSearchEngine engine;
	// The content search reports from several threads
	REQUIRE(engine.search({ .roots = { tree.path() }, .nameFilters = { QSL("*.txt") }, .contents = QSL("needle") }, &buffer));
	engine.waitForSearchToFinish();

	size_t matchCount = 0;
	for (;;)
	{
		const CFileSearchResultsBuffer::Batch batch = buffer.take(7);
		matchCount += batch.matches.size();
		if (batch.completion)
		{
			CHECK(batch.completion->status == CFileSearchEngine::SearchFinished);
			break;
		}

		REQUIRE(batch.matches.size() == 7); // Without the completion, a batch is full: the search is over
	}

	CHECK(matchCount == fileCount);
}

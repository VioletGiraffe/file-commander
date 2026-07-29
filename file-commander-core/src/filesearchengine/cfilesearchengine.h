#pragma once

#include "threading/cinterruptablethread.h"

#include <qcontainerfwd.h>

#include <atomic>
#include <stdint.h>

class QString;

class CFileSearchEngine
{
public:
	enum SearchStatus {
		SearchFinished,
		SearchCancelled,
		SearchInvalidPattern // The contents query was given as a regex and did not compile, so nothing was scanned
	};

	struct FileSearchListener {
		virtual ~FileSearchListener() noexcept = default;

		virtual void itemScanned(const QString& currentItem) = 0;
		// reachedThroughLink: the item was found by traversing a directory link, so the same file may also be
		// reported under its direct path if that one is within the search roots as well.
		virtual void matchFound(const QString& path, bool reachedThroughLink) = 0;
		virtual void searchFinished(SearchStatus status, uint64_t itemsScanned, uint64_t msElapsed) = 0;
	};

	bool searchInProgress() const;
	// An empty filter list matches any name, and an empty contentsToFind leaves the contents unexamined; only "where" is required.
	// Returns false having done nothing if there is nowhere to look, or if a search is already running - stopping that one is the caller's call.
	[[nodiscard]] bool search(
		const QStringList& filters, bool subjectCaseSensitive,
		const QStringList& where,
		const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
		FileSearchListener* listener);

	void stopSearching();
	// The worker holds the listener pointer, so anything that owns the listener must wait here before tearing it down
	void waitForSearchToFinish();

private:
	void searchThread(
		const QStringList& filters, bool subjectCaseSensitive,
		const QStringList& where,
		const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
		FileSearchListener* listener, const std::atomic<bool>& cancellationRequested) noexcept;

	// Clears the in-progress state before notifying, so the listener never sees "finished" while the engine still reports a search
	void notifySearchFinished(FileSearchListener* listener, SearchStatus status, uint64_t itemsScanned, uint64_t msElapsed);

private:
	CInterruptableThread _workerThread{ "File search thread" };
	// The worker outlives the search it reports: it is still unwinding after the listener has been told the search is over
	std::atomic<bool> _searchInProgress {false};
};


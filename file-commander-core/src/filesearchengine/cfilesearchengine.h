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
		SearchCancelled
	};

	struct FileSearchListener {
		virtual ~FileSearchListener() noexcept = default;

		virtual void itemScanned(const QString& currentItem) = 0;
		virtual void matchFound(const QString& path) = 0;
		virtual void searchFinished(SearchStatus status, uint64_t itemsScanned, uint64_t msElapsed) = 0;
	};

	bool searchInProgress() const;
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


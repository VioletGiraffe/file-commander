#pragma once

#include "cfilesearchengine.h"
#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <vector>

// Holds what a search reports on its threads until the UI thread takes it
class CFileSearchResultsBuffer final : public CFileSearchEngine::FileSearchListener
{
public:
	struct Match {
		CFileSystemObject item;
		bool reachedThroughLink;
	};

	struct Completion {
		CFileSearchEngine::SearchStatus status;
		uint64_t itemsScanned;
		uint64_t inconclusiveItems;
		uint64_t msElapsed;
	};

	struct Batch {
		std::vector<Match> matches;
		QString lastScannedItem; // Empty when no item was reported since the previous batch
		std::optional<Completion> completion; // Never before the search's last match
	};

	// requestTake: must schedule one take() on the UI thread, and return without calling it.
	// Called once per take() that is due, on a search thread or inside take(), possibly on both at the same time.
	// Empty for an owner that calls take() periodically.
	explicit CFileSearchResultsBuffer(std::function<void()> requestTake = {});

	// At most maxMatches matches, the oldest first. Requests the next take() itself while matches remain.
	[[nodiscard]] Batch take(size_t maxMatches);

	void itemScanned(const QString& currentItem) override;
	void matchFound(const CFileSystemObject& item, bool reachedThroughLink) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed) override;

private:
	// Makes the change under the mutex, then requests a take() unless one is due already
	template <typename Change>
	void addReport(Change&& change);

private:
	const std::function<void()> _requestTake; // May be empty

	std::mutex _mutex;
	// Guarded by _mutex
	std::deque<Match> _matches;
	QString _lastScannedItem;
	std::optional<Completion> _completion;
	bool _takeIsDue = false;
};

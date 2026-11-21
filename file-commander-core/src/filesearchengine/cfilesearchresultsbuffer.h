#pragma once

#include "cfilesearchengine.h"
#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <deque>
#include <mutex>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <vector>

// Holds what a search reports on its threads until the UI thread takes it
class CFileSearchResultsBuffer final : public CFileSearchEngine::FileSearchListener
{
public:
	struct Completion {
		CFileSearchEngine::SearchStatus status;
		uint64_t itemsScanned;
		uint64_t inconclusiveItems;
		uint64_t msElapsed;
	};

	struct Batch {
		std::vector<CFileSystemObject> matches;
		// The matches found through a directory link: the same item can also be listed under its direct path
		std::vector<uint64_t> hashesReachedThroughLink;
		QString lastScannedItem; // Empty when no item was reported since the previous batch
		std::optional<Completion> completion; // Never before the search's last match
	};

	// At most maxMatches matches, the oldest first
	[[nodiscard]] Batch take(size_t maxMatches);

	void itemScanned(const QString& currentItem) override;
	void matchFound(const CFileSystemObject& item, bool reachedThroughLink) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed) override;

private:
	struct Match {
		CFileSystemObject item;
		bool reachedThroughLink;
	};

	std::mutex _mutex;
	// Guarded by _mutex
	std::deque<Match> _matches;
	QString _lastScannedItem;
	std::optional<Completion> _completion;
};

[[nodiscard]] QString fileSearchCompletionText(const CFileSearchResultsBuffer::Completion& completion, uint64_t itemsFound);

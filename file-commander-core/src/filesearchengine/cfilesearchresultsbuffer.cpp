#include "cfilesearchresultsbuffer.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <iterator>
#include <utility>

CFileSearchResultsBuffer::Batch CFileSearchResultsBuffer::take(const size_t maxMatches)
{
	assert_r(maxMatches > 0);

	Batch batch;
	std::lock_guard lock{ _mutex };
	const auto sliceEnd = _matches.begin() + (ptrdiff_t)std::min(_matches.size(), maxMatches);
	batch.matches.assign(std::make_move_iterator(_matches.begin()), std::make_move_iterator(sliceEnd));
	_matches.erase(_matches.begin(), sliceEnd);

	batch.lastScannedItem = std::exchange(_lastScannedItem, {});
	if (_matches.empty())
		batch.completion = std::exchange(_completion, std::nullopt);

	return batch;
}

void CFileSearchResultsBuffer::itemScanned(const QString& currentItem)
{
	std::lock_guard lock{ _mutex };
	_lastScannedItem = currentItem;
}

void CFileSearchResultsBuffer::matchFound(const CFileSystemObject& item, bool reachedThroughLink)
{
	std::lock_guard lock{ _mutex };
	_matches.push_back({ item, reachedThroughLink });
}

void CFileSearchResultsBuffer::searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed)
{
	std::lock_guard lock{ _mutex };
	_completion = Completion{ status, itemsScanned, inconclusiveItems, msElapsed };
}

#include "cfilesearchresultsbuffer.h"

// Submodule includes
#include "assert/advanced_assert.h"


#include <algorithm>
#include <iterator>
#include <utility>

CFileSearchResultsBuffer::CFileSearchResultsBuffer(std::function<void()> requestTake) :
	_requestTake{ std::move(requestTake) }
{
}

CFileSearchResultsBuffer::Batch CFileSearchResultsBuffer::take(const size_t maxMatches)
{
	assert_r(maxMatches > 0);

	Batch batch;
	{
		std::lock_guard lock{ _mutex };
		const auto sliceEnd = _matches.begin() + (ptrdiff_t)std::min(_matches.size(), maxMatches);
		batch.matches.assign(std::make_move_iterator(_matches.begin()), std::make_move_iterator(sliceEnd));
		_matches.erase(_matches.begin(), sliceEnd);

		batch.lastScannedItem = std::exchange(_lastScannedItem, {});

		if (_matches.empty())
		{
			batch.completion = std::exchange(_completion, std::nullopt);
			_takeIsDue = false;
			return batch;
		}
	}

	if (_requestTake)
		_requestTake();
	return batch;
}

template <typename Change>
void CFileSearchResultsBuffer::addReport(Change&& change)
{
	{
		std::lock_guard lock{ _mutex };
		change();
		if (std::exchange(_takeIsDue, true))
			return;
	}

	// Outside the mutex: the request takes locks of its own
	if (_requestTake)
		_requestTake();
}

void CFileSearchResultsBuffer::itemScanned(const QString& currentItem)
{
	addReport([&] { _lastScannedItem = currentItem; });
}

void CFileSearchResultsBuffer::matchFound(const CFileSystemObject& item, bool reachedThroughLink)
{
	addReport([&] { _matches.push_back({ item, reachedThroughLink }); });
}

void CFileSearchResultsBuffer::searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed)
{
	addReport([&] { _completion = Completion{ status, itemsScanned, inconclusiveItems, msElapsed }; });
}

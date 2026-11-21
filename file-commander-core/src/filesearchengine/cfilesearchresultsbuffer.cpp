#include "cfilesearchresultsbuffer.h"

// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QObject>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

CFileSearchResultsBuffer::Batch CFileSearchResultsBuffer::take(const size_t maxMatches)
{
	assert_r(maxMatches > 0);

	Batch batch;
	std::lock_guard lock{ _mutex };
	const auto sliceEnd = _matches.begin() + (ptrdiff_t)std::min(_matches.size(), maxMatches);
	batch.matches.reserve((size_t)(sliceEnd - _matches.begin()));
	for (auto match = _matches.begin(); match != sliceEnd; ++match)
	{
		if (match->reachedThroughLink)
			batch.hashesReachedThroughLink.push_back(match->item.hash());
		batch.matches.push_back(std::move(match->item));
	}
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

// "1 item", "2 items"
[[nodiscard]] static QString itemCountText(const uint64_t count)
{
	return count == 1 ? QObject::tr("1 item") : QObject::tr("%1 items").arg(count);
}

QString fileSearchCompletionText(const CFileSearchResultsBuffer::Completion& completion, const uint64_t itemsFound)
{
	if (completion.status == CFileSearchEngine::SearchInvalidPattern)
		return QObject::tr("The contents pattern is not a valid regular expression");

	QString text = completion.status == CFileSearchEngine::SearchCancelled ? QObject::tr("Search aborted") : QObject::tr("Search completed");
	text += QObject::tr(", %1 found").arg(itemCountText(itemsFound));
	// Neither matches nor known non-matches, so saying nothing would present the results as complete
	if (completion.inconclusiveItems > 0)
		text += QObject::tr(", %1 could not be searched").arg(itemCountText(completion.inconclusiveItems));
	text += QObject::tr(": %1 scanned in %2 sec").arg(itemCountText(completion.itemsScanned)).arg((double)completion.msElapsed * 1e-3, 0, 'f', 1);
	return text;
}

#pragma once

#include "threading/cinterruptablethread.h"

#include <qcontainerfwd.h>

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
		const QString& what, bool subjectCaseSensitive,
		const QStringList& where,
		const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
		FileSearchListener* listener);

	void stopSearching();

private:
	void searchThread(
		const QString& what, bool subjectCaseSensitive,
		const QStringList& where,
		const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords, bool contentsIsRegex,
		FileSearchListener* listener) noexcept;

private:
	CInterruptableThread _workerThread{ "File search thread" };
};


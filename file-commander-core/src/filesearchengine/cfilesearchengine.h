#pragma once

#include "threading/cinterruptablethread.h"

#include <qcontainerfwd.h>

class CController;

class QString;

#include <set>

class CController;
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
		virtual void searchFinished(SearchStatus status, uint32_t itemsPerSecond) = 0;
	};

	explicit CFileSearchEngine(CController& controller);
	void addListener(FileSearchListener* listener);
	void removeListener(FileSearchListener* listener);


	bool searchInProgress() const;
	[[nodiscard]] bool search(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords);
	void stopSearching();

private:
	void searchThread(const QString& what, bool subjectCaseSensitive, const QStringList& where, const QString& contentsToFind, bool contentsCaseSensitive, bool contentsWholeWords) noexcept;

private:
	CController& _controller;

	CInterruptableThread _workerThread;
	std::set<FileSearchListener*> _listeners;
};


#pragma once

#include "threading/cinterruptablethread.h"

#include <set>
#include <vector>

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
		virtual ~FileSearchListener() {}

		virtual void itemScanned(const QString& currentItem) = 0;
		virtual void matchFound(const QString& path) = 0;
		virtual void searchFinished(SearchStatus status, uint32_t itemsPerSecond) = 0;
	};

	CFileSearchEngine(CController& controller);
	void addListener(FileSearchListener* listener);
	void removeListener(FileSearchListener* listener);


	bool searchInProgress() const;
	void search(const QString& what, bool subjectCaseSensitive, const std::vector<QString>& where, const QString& contentsToFind, bool contentsCaseSensitive);
	void stopSearching();

private:
	CController& _controller;

	CInterruptableThread _workerThread;
	std::set<FileSearchListener*> _listeners;
};


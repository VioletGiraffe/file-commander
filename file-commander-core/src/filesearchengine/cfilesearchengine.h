#pragma once

#include "threading/cinterruptablethread.h"

#include <set>

class CController;
class QString;

class CFileSearchEngine
{
public:
	struct FileSearchListener {
		virtual ~FileSearchListener() {}

		virtual void itemScanned(const QString& currentItem) const = 0;
		virtual void matchFound(const QString& path) const = 0;
		virtual void searchFinished() const = 0;
	};

	CFileSearchEngine(CController& controller);
	void addListener(FileSearchListener* listener);
	void removeListener(FileSearchListener* listener);


	bool searchInProgress() const;
	void search(const QString& what, const QString& where, const QString& contentsToFind);
	void stopSearching();

private:
	CController& _controller;

	CInterruptableThread _workerThread;
	std::set<FileSearchListener*> _listeners;
};


#pragma once
#define _VARIADIC_MAX 10

#include "operationcodes.h"
#include "../cfilesystemobject.h"
#include "../utils/ctimeelapsed.h"

#include "../../include/QtCoreIncludes"

#include <assert.h>
#include <vector>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class CFileOperationObserver
{
friend class COperationPerformer;
public:
	CFileOperationObserver() {}

	virtual void onProgressChanged(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int filePercentage, uint64_t speed /* B/s*/) = 0;
	virtual void onProcessHalted(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage) = 0; // User decision required (file exists, file is read-only etc.)
	virtual void onProcessFinished() = 0; // Done or canceled
	virtual void onCurrentFileChanged(QString file) = 0; // Starting to process a new file

	virtual ~CFileOperationObserver() {}

	inline std::mutex& callbackMutex() { return _callbackMutex; }

private:
	void onProgressChangedCallback(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int filePercentage, uint64_t speed /* B/s*/) {
		assert(filePercentage <= 100 && totalPercentage <= 100);
		std::lock_guard<std::mutex> lock(_callbackMutex); _callbacks.emplace_back(std::bind(&CFileOperationObserver::onProgressChanged, this, totalPercentage, numFilesProcessed, totalNumFiles, filePercentage, speed));
	}
	void onProcessHaltedCallback(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage) {
		std::lock_guard<std::mutex> lock(_callbackMutex); _callbacks.emplace_back(std::bind(&CFileOperationObserver::onProcessHalted, this, reason, source, dest, errorMessage));
	}
	void onProcessFinishedCallback() {
		std::lock_guard<std::mutex> lock(_callbackMutex); _callbacks.emplace_back(std::bind(&CFileOperationObserver::onProcessFinished, this));
	}
	void onCurrentFileChangedCallback(QString file) {
		std::lock_guard<std::mutex> lock(_callbackMutex); _callbacks.emplace_back(std::bind(&CFileOperationObserver::onCurrentFileChanged, this, file));
	}

protected:
	std::vector<std::function<void ()> > _callbacks;
	std::mutex                           _callbackMutex;
};

class COperationPerformer
{
public:
	COperationPerformer(Operation operation, std::vector<CFileSystemObject> source, QString destination = QString());
	~COperationPerformer();

	void setWatcher(CFileOperationObserver *watcher);

	bool togglePause();
	bool paused()  const;
	bool working() const;
	bool done()    const;

	// User can supply a new name (not full path)
	void userResponse(HaltReason haltReason, UserResponse response, QString newName = QString());

// Operations
	void start();
	void cancel();

private:
	void threadFunc();
	void waitForResponse();

	void copyFiles();
	void deleteFiles();

	void finalize();

	// Iterates over all dirs in the source vector, and their subdirs, and so on and replaces _sources with a flat list of files. Returns a list of destination folders where each of the files must be copied to according to _dest
	// Also counts the total size of all the files to monitor progress
	std::vector<QDir> flattenSourcesAndCalcDest(uint64_t& totalSize);

private:
	std::vector<CFileSystemObject> _source;
	std::map<HaltReason, UserResponse> _globalResponses;
	QString                        _dest, _newName;
	Operation                      _op;
	std::atomic<bool>              _paused;
	std::atomic<bool>              _inProgress;
	std::atomic<bool>              _finished;
	std::atomic<bool>              _cancelRequested;
	UserResponse                   _userResponse;

	std::thread                    _thread;
	std::mutex                     _waitForResponseMutex;
	std::condition_variable        _waitForResponseCondition;

	CFileOperationObserver       * _observer;

	// For calculating copy / move speed
	CTimeElapsed                  _totalTimeElapsed;
	CTimeElapsed                  _fileTimeElapsed;
};

QDir destinationFolder (const QString& absoluteSourcePath, const QString& originPath, const QString& baseDestPath, bool sourceIsDir);

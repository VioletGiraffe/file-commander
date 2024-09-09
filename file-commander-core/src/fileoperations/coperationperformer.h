#pragma once

#include "operationcodes.h"
#include "cfilesystemobject.h"
#include "system/ctimeelapsed.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

DISABLE_COMPILER_WARNINGS
#include <QDebug>
RESTORE_COMPILER_WARNINGS

class CFileOperationObserver
{
friend class COperationPerformer;
public:
	virtual void onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* B/s*/, uint32_t secondsRemaining) = 0;
	virtual void onProcessHalted(HaltReason reason, const CFileSystemObject& source, const CFileSystemObject& dest, const QString& errorMessage) = 0; // User decision required (file exists, file is read-only etc.)
	virtual void onProcessFinished(const QString& message) = 0; // Done or canceled
	virtual void onCurrentFileChanged(const QString& file) = 0; // Starting to process a new file

	virtual ~CFileOperationObserver() = default;

	void processEvents();

private:
	void onProgressChangedCallback(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* B/s*/, uint32_t secondsRemaining);
	void onProcessHaltedCallback(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage);
	void onProcessFinishedCallback(QString message = {});
	void onCurrentFileChangedCallback(QString file);

private:
	std::vector<std::function<void ()>> _callbacks;
	std::mutex                          _callbackMutex;
};

class COperationPerformer
{
public:
	COperationPerformer(Operation operation, std::vector<CFileSystemObject>&& source, QString destination = QString());
	COperationPerformer(Operation operation, const CFileSystemObject& source, QString destination = QString());
	~COperationPerformer();

	COperationPerformer(const COperationPerformer&) = delete;
	COperationPerformer& operator=(const COperationPerformer&) = delete;

	void setObserver(CFileOperationObserver *observer);

	[[nodiscard]] bool togglePause();
	[[nodiscard]] bool paused()  const;
	[[nodiscard]] bool working() const;
	[[nodiscard]] bool done()    const;

	// User can supply a new name (not full path)
	void userResponse(HaltReason haltReason, UserResponse response, QString newName = QString());

// Operations
	void start();
	void cancel();

private:
	void thread();
	void waitForResponse();

	void copyFiles();
	void deleteFiles();
	void moveWithinSameDrive();

	void finalize();

	// Iterates over all dirs in the source vector, and their subdirs, and so on and replaces _sources with a flat list of files. Returns a list of destination folders where each of the files must be copied to according to _dest
	// Also counts the total size of all the files to monitor progress
	[[nodiscard]] std::vector<QDir> enumerateSourcesAndCalcDest(uint64_t& totalSize);

	[[nodiscard]] UserResponse getUserResponse(HaltReason hr, const CFileSystemObject& src, const CFileSystemObject& dst, const QString& message);

// Suboperation handlers
	enum NextAction {naProceed, naRetryItem, naRetryOperation, naSkip, naAbort};
	[[nodiscard]] NextAction deleteItem(CFileSystemObject& item);
	[[nodiscard]] NextAction makeItemWriteable(CFileSystemObject& item);
	[[nodiscard]] NextAction copyItem(CFileSystemObject& item, const QFileInfo& destInfo, const QDir& destDir, uint64_t sizeProcessedPreviously, uint64_t totalSize, size_t currentItemIndex);
	[[nodiscard]] NextAction mkPath(const QDir& dir);

	void handlePause();

private:
	struct ObjectToProcess {
		inline explicit ObjectToProcess(CFileSystemObject&& fso) : object{ std::move(fso) }
		{}

		inline explicit ObjectToProcess(const CFileSystemObject& fso) : object{ fso }
		{}

		CFileSystemObject object;
		bool copiedSuccessfully = false;
	};

	friend QDebug& operator<<(QDebug& stream, const std::vector<ObjectToProcess>& objects);

private:
	std::vector<ObjectToProcess> _source;
	std::map<HaltReason, UserResponse> _globalResponses;
	CFileSystemObject              _destFileSystemObject;
	QString                        _newName;
	Operation                      _op;
	std::atomic<bool>              _paused {false};
	std::atomic<bool>              _inProgress {false};
	std::atomic<bool>              _done {false};
	std::atomic<bool>              _cancelRequested {false};
	UserResponse                   _userResponse = urNone;

	std::thread                    _thread;
	std::mutex                     _waitForResponseMutex;
	std::condition_variable        _waitForResponseCondition;

	CFileOperationObserver       * _observer = nullptr;

	// For calculating copy / move speed
	CTimeElapsed                  _totalTimeElapsed;
};

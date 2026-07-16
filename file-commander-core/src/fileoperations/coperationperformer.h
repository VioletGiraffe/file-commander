#pragma once

#include "operationcodes.h"
#include "cfilesystemobject.h"
#include "system/ctimeelapsed.h"

#include "3rdparty/magic_enum/magic_enum.hpp"
#include "fs.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

class QDebug;
class QFileInfo;

class CFileOperationObserver
{
friend class COperationPerformer;
public:
	virtual void onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* bytes/s for copy & move, items/s for delete */, uint32_t secondsRemaining) = 0;
	virtual void onProcessHalted(HaltReason reason, const CFileSystemObject& source, const CFileSystemObject& dest, const QString& errorMessage) = 0; // User decision required (file exists, file is read-only etc.)
	virtual void onProcessFinished(const QString& message) = 0; // Done or canceled
	virtual void onCurrentFileChanged(const QString& file) = 0; // Starting to process a new file

	virtual ~CFileOperationObserver() = default;

	void processEvents();

private:
	struct ProgressEvent {
		float totalPercentage;
		size_t numFilesProcessed;
		size_t totalNumFiles;
		float filePercentage;
		uint64_t speed;
		uint32_t secondsRemaining;
	};

	struct StateEvent {
		std::optional<ProgressEvent> progress;
		std::optional<QString> currentFile;
	};

	struct HaltEvent {
		HaltReason reason;
		CFileSystemObject source;
		CFileSystemObject dest;
		QString errorMessage;
		std::shared_ptr<const std::atomic<bool>> cancellationRequested;
	};

	struct FinishedEvent {
		QString message;
	};

	using Event = std::variant<StateEvent, HaltEvent, FinishedEvent>;

	friend struct CFileOperationObserverTestSeam;

	void onProgressChangedCallback(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* bytes/s for copy & move, items/s for delete */, uint32_t secondsRemaining);
	void onProcessHaltedCallback(HaltReason reason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage, std::shared_ptr<const std::atomic<bool>> cancellationRequested);
	void onProcessFinishedCallback(QString message = {});
	void onCurrentFileChangedCallback(QString file);

private:
	std::vector<Event> _events;
	std::mutex         _eventMutex;
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
	// Test-only access to the private state (e.g. _forceMoveByCopy); defined in the test code
	friend struct COperationPerformerTestSeam;

	void thread();
	UserResponse waitForResponse();

	void copyFiles();
	void deleteFiles();
	void moveWithinSameDrive();

	void finalize();
	// Writes the timestamps collected in _pendingDirectoryTimes. Failures are reported through _completionMessage
	// rather than aborting: the folder's contents are already in place, which is what the operation was asked to do.
	void applyPendingDirectoryTimes();

	// Iterates over all dirs in the source vector, and their subdirs, and so on and replaces _sources with a flat list of files. Returns a list of destination folders where each of the files must be copied to according to _dest
	// Also counts the total size of all the files to monitor progress
	[[nodiscard]] std::vector<QDir> enumerateSourcesAndCalcDest(uint64_t& totalSize);

	[[nodiscard]] UserResponse getUserResponse(HaltReason hr, const CFileSystemObject& src, const CFileSystemObject& dst, const QString& message);

// Suboperation handlers
	// naAbort: the user chose to abort in a prompt; naCancel: cancel() was requested while processing the item
	enum NextAction {naProceed, naRetryItem, naRetryOperation, naSkip, naAbort, naCancel};
	[[nodiscard]] NextAction deleteItem(CFileSystemObject& item);
	[[nodiscard]] NextAction materializeDestinationDirectory(const CFileSystemObject& source, const QFileInfo& destination);
	[[nodiscard]] NextAction removeDestinationForReplacement(CFileSystemObject& destination);
	[[nodiscard]] NextAction makeItemWriteable(CFileSystemObject& item);
	[[nodiscard]] NextAction copyItem(CFileSystemObject& item, const QFileInfo& destInfo, const QDir& destDir, uint64_t sizeProcessedPreviously, uint64_t totalSize, size_t currentItemIndex);
	[[nodiscard]] NextAction mkPath(const QDir& dir);

	// Returns false if cancellation was requested while waiting or before this boundary.
	[[nodiscard]] bool waitWhilePaused();

private:
	struct ObjectToProcess {
		inline explicit ObjectToProcess(CFileSystemObject&& fso) : object{ std::move(fso) }
		{}

		inline explicit ObjectToProcess(const CFileSystemObject& fso) : object{ fso }
		{}

		CFileSystemObject object;
		bool materializedSuccessfully = false;
		// The item was found by traversing a directory link; it must be copied (materialized), but never deleted by move
		bool reachedThroughLink = false;
	};

	// A destination folder this operation created, and the source timestamps it must end up with. Deferred to the end
	// of the operation: creating an entry inside a folder updates that folder's modification time, so stamping it any
	// earlier would be undone by its own contents being copied in.
	struct PendingDirectoryTimes {
		QString destinationPath;
		thin_io::entry_times times;
	};

	friend QDebug& operator<<(QDebug& stream, const std::vector<ObjectToProcess>& objects);

private:
	std::vector<ObjectToProcess> _source;
	std::vector<PendingDirectoryTimes> _pendingDirectoryTimes;
	// Essentially a map<HaltReason, UserResponse>
	std::array<std::optional<UserResponse>, magic_enum::enum_count<HaltReason>()> _globalResponses;
	CFileSystemObject              _destFileSystemObject;
	QString                        _newName;
	Operation                      _op;
	std::atomic<bool>              _paused {false};
	std::atomic<bool>              _inProgress {false};
	std::atomic<bool>              _done {false};
	std::shared_ptr<std::atomic<bool>> _cancellationRequested = std::make_shared<std::atomic<bool>>(false);
	// Forces a move to take the chunked copy+delete path even when a same-drive rename is possible
	bool                           _forceMoveByCopy = false;
	bool                           _pauseAfterFirstCopyChunkForTest = false;
	bool                           _pauseBeforeDirectoryCleanupForTest = false;
	UserResponse                   _userResponse = urNone;

	std::thread                    _thread;
	std::mutex                     _waitForResponseMutex;
	std::condition_variable        _waitForResponseCondition;
	QString                        _completionMessage;

	CFileOperationObserver       * _observer = nullptr;

	// For calculating copy / move speed
	CTimeElapsed                  _totalTimeElapsed;
};

#pragma once

// Helpers shared by the COperationPerformer test .cpp files.
// Includes catch.hpp: the runner TU must #define CATCH_CONFIG_RUNNER before including this header.

#include "fileoperations/coperationperformer.h"
#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <utility>

#include "3rdparty/catch2/catch.hpp"

struct ProgressObserver : public CFileOperationObserver {
	inline void onProgressChanged(float totalPercentage, size_t /*numFilesProcessed*/, size_t /*totalNumFiles*/, float filePercentage, uint64_t /*speed*/ /* B/s*/, uint32_t /*secondsRemaining*/) override {
		CHECK(totalPercentage <= 100.1f);
		CHECK(filePercentage <= 100.1f);
	}
	inline void onProcessHalted(HaltReason /*reason*/, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override { // User decision required (file exists, file is read-only etc.)
		FAIL("onProcessHalted called");
	}
	inline void onProcessFinished(const QString& /*message*/ = QString()) override {} // Done or canceled
	inline void onCurrentFileChanged(const QString& /*file*/) override {} // Starting to process a new file
};

// Friend of COperationPerformer
struct COperationPerformerTestSeam {
	static void setForceMoveByCopy(COperationPerformer& p, const bool force) { p._forceMoveByCopy = force; }
};

struct CFileOperationObserverTestSeam {
	static void postProgress(CFileOperationObserver& observer, float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed, uint32_t secondsRemaining) {
		observer.onProgressChangedCallback(totalPercentage, numFilesProcessed, totalNumFiles, filePercentage, speed, secondsRemaining);
	}

	static void postHalt(CFileOperationObserver& observer, HaltReason reason, std::shared_ptr<const std::atomic<bool>> cancellationRequested) {
		observer.onProcessHaltedCallback(reason, CFileSystemObject(), CFileSystemObject(), {}, std::move(cancellationRequested));
	}

	static void postFinished(CFileOperationObserver& observer, QString message = {}) {
		observer.onProcessFinishedCallback(std::move(message));
	}

	static void postCurrentFile(CFileOperationObserver& observer, QString file) {
		observer.onCurrentFileChangedCallback(std::move(file));
	}

	static size_t pendingEventCount(CFileOperationObserver& observer) {
		std::lock_guard<std::mutex> lock(observer._eventMutex);
		return observer._events.size();
	}
};

inline void writeTestFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	REQUIRE(file.open(QFile::WriteOnly));
	REQUIRE(file.write(contents) == contents.size());
}

inline QByteArray readFileContents(const QString& path)
{
	QFile file(path);
	REQUIRE(file.open(QFile::ReadOnly));
	return file.readAll();
}

// Pumps the observer's event queue until the condition becomes true.
// On timeout, deliberately leaks the performer and the observer - the worker thread is likely stuck, and ~COperationPerformer
// would hang forever trying to join it - and fails the test, so that a hang is flagged red in-process.
template <typename ObserverT, typename ConditionT>
static void pumpUntil(std::unique_ptr<COperationPerformer>& p, std::unique_ptr<ObserverT>& observer, ConditionT&& condition)
{
	CTimeElapsed timer(true);
	while (!condition())
	{
		observer->processEvents();
		if (timer.elapsed<std::chrono::seconds>() > 30)
		{
			(void)p.release();
			(void)observer.release();
			FAIL("File operation timeout reached - the worker thread is likely stuck.");
		}
	}
}

template <typename ObserverT>
static void pumpOperationToCompletion(std::unique_ptr<COperationPerformer>& p, std::unique_ptr<ObserverT>& observer)
{
	pumpUntil(p, observer, [&p] { return p->done(); });

	// done() becomes true immediately before the worker posts its final observer event. Join the worker while the observer is still alive,
	// then dispatch that event so neither the worker nor a buffered callback can outlive the observer.
	p.reset();
	observer->processEvents();
}

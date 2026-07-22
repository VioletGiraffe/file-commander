#pragma once

#include "fileoperationtypes.h"

#include "threading/cinterruptablethread.h"

#include <condition_variable>
#include <mutex>
#include <variant>

// Everything the job delivers through processEvents(), in queue order. Repeated progress snapshots
// coalesce to the latest; DecisionRequest and OperationSummary are ordering barriers.
using OperationEvent = std::variant<ProgressSnapshot, DecisionRequest, OperationSummary>;

class CFileOperationListener
{
public:
	virtual ~CFileOperationListener() = default;
	virtual void onOperationEvent(const OperationEvent& event) = 0;
};

// Derived presentation state. Pause and decision waiting are deliberately not in it: the dialog sets
// pause from its own UI state, and the pending decision has its own query.
enum class JobStatus
{
	NotStarted,
	Running,
	Finished
};

// The one owner of all cross-thread file-operation state: one worker thread, one mutex covering both
// the control state and the event queue, one condition variable waking resume, cancellation, and
// decision submission. Wraps the synchronous executors; the wrapper thread's flag is the only
// cancellation predicate - there is no duplicate job-side cancellation Boolean.
class CFileOperationJob
{
public:
	explicit CFileOperationJob(FileOperationRequest request, uint64_t transferChunkSize = 8 * 1024 * 1024);
	~CFileOperationJob(); // Requests cancellation, wakes every wait, joins

	CFileOperationJob(const CFileOperationJob&) = delete;
	CFileOperationJob& operator=(const CFileOperationJob&) = delete;

	// One-shot; the job never reuses its thread.
	void start();
	// Never joins. Sets the wrapper's flag under the job mutex, so no wait can evaluate its predicate
	// between the store and blocking - a lost wakeup is unrepresentable. Also drops any undrained
	// decision event: cancellation makes it unanswerable. Valid in any state: a cancel() before start()
	// is remembered and applied there, past the wrapper's flag reset.
	void cancel();
	void setPaused(bool paused);
	// False = no decision is pending (a late response after cancellation invalidated the prompt).
	// There are no decision IDs: the worker cannot reach a second decision while blocked on the first.
	bool submitDecision(Decision decision);

	[[nodiscard]] JobStatus status() const;
	// The dialog's pre-show check: a DecisionRequest already swapped out of the queue must not be
	// presented once cancellation has invalidated it.
	[[nodiscard]] bool hasPendingDecision() const;

	// Swaps the queued events out under the mutex, then dispatches with the mutex released: a modal
	// prompt may enter a nested event loop and call back into the job.
	void processEvents(CFileOperationListener& listener);

private:
	// --- Worker side. The flag reference is the wrapper's cancellation flag, passed into the payload. ---

	void runOperation(const std::atomic<bool>& cancellationRequested);
	[[nodiscard]] bool workerCheckpoint(const std::atomic<bool>& cancellationRequested);
	[[nodiscard]] std::optional<Decision> workerRequestDecision(DecisionRequest request, const std::atomic<bool>& cancellationRequested);
	void enqueueProgress(const ProgressSnapshot& snapshot);

	const FileOperationRequest _request;
	const uint64_t _transferChunkSize;

	mutable std::mutex _mutex;
	std::condition_variable _stateChanged;

	bool _started = false;
	bool _finished = false; // Set only after the summary event is queued: Finished implies the summary is observable
	bool _cancelledBeforeStart = false; // Write-once, consumed by start(); never read as a runtime cancellation predicate
	bool _pauseRequested = false;
	std::optional<DecisionRequest> _pendingRequest;
	std::optional<Decision> _submittedDecision;

	std::vector<OperationEvent> _events;

	CInterruptableThread _thread{ "File operation" };
};

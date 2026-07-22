#include "cfileoperationjob.h"
#include "cdeleteexecutor.h"
#include "coperationexecutioncontext.h"
#include "ctransferexecutor.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

#include <algorithm>

CFileOperationJob::CFileOperationJob(FileOperationRequest request, const uint64_t transferChunkSize) :
	_request{ mv(request) },
	_transferChunkSize{ transferChunkSize }
{
}

CFileOperationJob::~CFileOperationJob()
{
	{
		std::lock_guard lock{ _mutex };
		_thread.requestCancellation();
	}
	_stateChanged.notify_all();
	_thread.join();
}

void CFileOperationJob::start()
{
	std::lock_guard lock{ _mutex };
	assert_r(!_started);
	_started = true;
	// Under the mutex: _thread.start() resets the wrapper's cancellation flag, so a concurrent cancel()
	// must not interleave with it. The newborn worker cannot deadlock here: it touches this mutex only
	// from its context callbacks.
	_thread.start([this](const std::atomic<bool>& cancellationRequested) { runOperation(cancellationRequested); });
	// Past the reset: a pre-start cancel() takes effect now, before the worker's first checkpoint can
	// run (it reads the flag under this same mutex), so nothing is ever mutated.
	if (_cancelledBeforeStart)
		_thread.requestCancellation();
}

void CFileOperationJob::cancel()
{
	{
		std::lock_guard lock{ _mutex };
		if (!_started)
		{
			// The wrapper's flag would be wiped by start()'s reset; remember the request for start() to apply.
			_cancelledBeforeStart = true;
			return;
		}
		_thread.requestCancellation();
		std::erase_if(_events, [](const OperationEvent& event) { return std::holds_alternative<DecisionRequest>(event); });
	}
	_stateChanged.notify_all();
}

void CFileOperationJob::setPaused(const bool paused)
{
	{
		std::lock_guard lock{ _mutex };
		if (_pauseRequested == paused)
			return;
		_pauseRequested = paused;
	}
	if (!paused)
		_stateChanged.notify_all();
}

bool CFileOperationJob::submitDecision(Decision decision)
{
	std::unique_lock lock{ _mutex };
	if (!_pendingRequest || _thread.cancellationRequested())
		return false; // A late response: the prompt has been invalidated

	// The UI builds its controls from the delivered request, so anything outside it is a caller bug.
	const DecisionRequest& request = *_pendingRequest;
	const bool actionAllowed = std::ranges::find(request.allowedActions, decision.action) != request.allowedActions.end();
	const bool scopeAllowed = decision.scope == DecisionScope::ThisItem
		|| (request.remainingMatchingScopeAllowed && isActionRememberable(request.issue.kind, decision.action));
	const bool nameValid = decision.action != DecisionAction::Rename || (decision.newName && !decision.newName->isEmpty());
	assert_r(actionAllowed && scopeAllowed && nameValid);
	if (!actionAllowed || !scopeAllowed || !nameValid)
		return false;

	_submittedDecision = mv(decision);
	lock.unlock();
	_stateChanged.notify_all();
	return true;
}

JobStatus CFileOperationJob::status() const
{
	std::lock_guard lock{ _mutex };
	if (!_started)
		return JobStatus::NotStarted;
	return _finished ? JobStatus::Finished : JobStatus::Running;
}

bool CFileOperationJob::hasPendingDecision() const
{
	std::lock_guard lock{ _mutex };
	return _pendingRequest.has_value() && !_thread.cancellationRequested();
}

void CFileOperationJob::processEvents(CFileOperationListener& listener)
{
	std::vector<OperationEvent> drained;
	{
		std::lock_guard lock{ _mutex };
		drained.swap(_events);
	}

	for (const OperationEvent& event : drained)
		listener.onOperationEvent(event);
}

void CFileOperationJob::runOperation(const std::atomic<bool>& cancellationRequested)
{
	COperationExecutionContext context{
		std::holds_alternative<PermanentDeleteRequest>(_request) ? PrimaryProgressUnit::Items : PrimaryProgressUnit::Bytes,
		[this, &cancellationRequested] { return workerCheckpoint(cancellationRequested); },
		[this, &cancellationRequested](const DecisionRequest& request) { return workerRequestDecision(request, cancellationRequested); },
		[this](const ProgressSnapshot& snapshot) { enqueueProgress(snapshot); }
	};

	OperationSummary summary = std::visit([&]<typename Request>(const Request& request) {
		if constexpr (std::is_same_v<Request, TransferRequest>)
			return CTransferExecutor{ context, _transferChunkSize }.run(request);
		else
			return CDeleteExecutor{ context }.run(request);
	}, _request);

	std::lock_guard lock{ _mutex };
	_events.emplace_back(mv(summary)); // The summary precedes the status flip: Finished implies it is queued
	_finished = true;
}

bool CFileOperationJob::workerCheckpoint(const std::atomic<bool>& cancellationRequested)
{
	std::unique_lock lock{ _mutex };
	_stateChanged.wait(lock, [this, &cancellationRequested] { return !_pauseRequested || cancellationRequested; });
	return !cancellationRequested;
}

std::optional<Decision> CFileOperationJob::workerRequestDecision(DecisionRequest request, const std::atomic<bool>& cancellationRequested)
{
	std::unique_lock lock{ _mutex };
	assert_r(!_pendingRequest && !_submittedDecision);

	if (cancellationRequested)
		return {};

	_pendingRequest = request;
	_events.emplace_back(mv(request)); // A barrier event: appended, never coalesced over

	_stateChanged.wait(lock, [this, &cancellationRequested] { return _submittedDecision.has_value() || cancellationRequested; });

	_pendingRequest.reset();
	std::optional<Decision> decision = mv(_submittedDecision);
	_submittedDecision.reset();

	// Cancellation wins even over a decision that arrived in the same wakeup: it terminates the job
	// rather than advancing it.
	if (cancellationRequested)
		return {};
	return decision;
}

void CFileOperationJob::enqueueProgress(const ProgressSnapshot& snapshot)
{
	std::lock_guard lock{ _mutex };
	if (!_events.empty() && std::holds_alternative<ProgressSnapshot>(_events.back()))
		_events.back() = snapshot; // Repeated progress coalesces to the latest; barriers stay in place
	else
		_events.emplace_back(snapshot);
}

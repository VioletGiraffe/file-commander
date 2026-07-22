#pragma once

#include "fileoperationtypes.h"

#include <array>
#include <chrono>
#include <functional>

// Timing and arithmetic for Working-phase progress; Scanning snapshots come from the tree builder.
// Receives semantic updates and assembles the snapshots the dialog renders. Time spent paused or
// waiting for a decision is excluded from speed and ETA.
class COperationProgress
{
public:
	explicit COperationProgress(PrimaryProgressUnit primaryUnit) noexcept;

	// Only once every required manifest is known; a partial aggregate must never look exact.
	void setTotals(uint64_t bytesTotal, size_t itemsTotal) noexcept;

	void setCurrentEntry(CEntryPath entry, std::optional<uint64_t> bytesTotal);
	void clearCurrentEntry();
	// Un-counts the current entry's partial progress (a failed attempt whose staging was discarded),
	// so a retry does not double-count the bytes.
	void currentEntryAbandoned() noexcept;

	void fileTransferAdvanced(uint64_t bytes) noexcept;
	void itemCompleted() noexcept;
	// Skipped or already-satisfied work: advances the processed totals without entering the speed basis.
	void advanceWithoutTransfer(uint64_t bytes, size_t items) noexcept;

	void waitStarted() noexcept;
	void waitEnded() noexcept;

	[[nodiscard]] ProgressSnapshot makeSnapshot() const;

private:
	[[nodiscard]] std::chrono::steady_clock::duration activeDuration() const noexcept;

	const PrimaryProgressUnit _primaryUnit;

	uint64_t _bytesProcessed = 0;
	size_t _itemsProcessed = 0;
	std::optional<uint64_t> _bytesTotal;
	std::optional<size_t> _itemsTotal;

	std::optional<CEntryPath> _currentEntry;
	uint64_t _currentEntryBytesProcessed = 0;
	std::optional<uint64_t> _currentEntryBytesTotal;

	uint64_t _transferredPrimaryUnits = 0; // The speed basis: genuinely performed work only

	std::chrono::steady_clock::time_point _activeSince;
	std::chrono::steady_clock::duration _accumulatedActive{ 0 };
	bool _waiting = false;
};

// The one concrete seam between the synchronous executors and their environment. The job (WP9) binds the
// callbacks to its mutex/condition-variable/event queue; synchronous tests bind them to scripted providers.
// Owns the remembered-decision table, the progress tracker, and the warning/failure/summary accumulation.
// Not an application-level service: private to the file-operation module.
class COperationExecutionContext
{
public:
	// Returns false when cancellation has won; a pause blocks inside the callback.
	using CheckpointCallback = std::function<bool()>;
	// Presents one decision request; nullopt means cancellation overrode the wait.
	using DecisionCallback = std::function<std::optional<Decision>(const DecisionRequest&)>;
	using ProgressCallback = std::function<void(const ProgressSnapshot&)>;

	COperationExecutionContext(PrimaryProgressUnit primaryUnit,
		CheckpointCallback checkpoint, DecisionCallback decisionProvider, ProgressCallback progressPublisher);

	// False = cancellation; no new mutation may start.
	[[nodiscard]] bool checkpoint();

	// The one decision entry point. Builds the DecisionRequest from the normative table, consults the
	// remembered table first (only when remainingMatchingScopeAllowed), forwards to the decision provider
	// otherwise, and stores a RemainingMatchingIssues response of a rememberable action.
	// A request that disallows the scope neither consults nor updates remembered state, and a
	// RemainingMatchingIssues response to it is recorded as ThisItem.
	// Decision waits are excluded from progress speed/ETA.
	// nullopt = cancellation overrode the decision.
	[[nodiscard]] std::optional<Decision> resolveDecision(OperationIssue issue, bool remainingMatchingScopeAllowed = true);

	[[nodiscard]] COperationProgress& progress() noexcept { return _progress; }
	void publishProgress(const ProgressSnapshot& snapshot);
	// Publishes the progress tracker's current Working snapshot.
	void publishProgressSnapshot();

	// Bounded accumulation: every call counts, only the first few diagnostics are retained verbatim.
	void recordWarning(OperationDiagnostic diagnostic);
	void recordFailure(OperationDiagnostic diagnostic);

	void addCompletedItems(const size_t count) noexcept { _accumulatedSummary.completedItems += count; }
	void addSkippedItems(const size_t count) noexcept { _accumulatedSummary.skippedItems += count; }
	void addAlreadySatisfiedItems(const size_t count) noexcept { _accumulatedSummary.alreadySatisfiedItems += count; }
	void addTransferredBytes(const uint64_t bytes) noexcept { _accumulatedSummary.transferredBytes += bytes; }

	// Failed items are counted by recordFailure: every originating failed item records exactly one
	// terminal diagnostic, and propagated parent outcomes record none.
	[[nodiscard]] OperationSummary makeSummary(CompletionStatus status) const;

private:
	CheckpointCallback _checkpoint;
	DecisionCallback _decisionProvider;
	ProgressCallback _progressPublisher;

	COperationProgress _progress;

	// Keyed by IssueKind; only actions rememberable per the normative table are ever stored.
	std::array<std::optional<DecisionAction>, 6> _rememberedDecisions;

	OperationSummary _accumulatedSummary;
};

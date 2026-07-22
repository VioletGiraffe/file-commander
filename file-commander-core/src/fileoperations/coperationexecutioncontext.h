#pragma once

#include "fileoperationtypes.h"

#include <array>
#include <functional>

// The one concrete seam between the synchronous executors and their environment. The job (WP9) binds the
// callbacks to its mutex/condition-variable/event queue; synchronous tests bind them to scripted providers.
// Owns the remembered-decision table and the warning/failure/summary accumulation. Not an application-level
// service: private to the file-operation module.
class COperationExecutionContext
{
public:
	// Returns false when cancellation has won; a pause blocks inside the callback.
	using CheckpointCallback = std::function<bool()>;
	// Presents one decision request; nullopt means cancellation overrode the wait.
	using DecisionCallback = std::function<std::optional<Decision>(const DecisionRequest&)>;
	using ProgressCallback = std::function<void(const ProgressSnapshot&)>;

	COperationExecutionContext(CheckpointCallback checkpoint, DecisionCallback decisionProvider, ProgressCallback progressPublisher);

	// False = cancellation; no new mutation may start.
	[[nodiscard]] bool checkpoint();

	// The one decision entry point. Builds the DecisionRequest from the normative table, consults the
	// remembered table first (only when remainingMatchingScopeAllowed), forwards to the decision provider
	// otherwise, and stores a RemainingMatchingIssues response of a rememberable action.
	// A request that disallows the scope neither consults nor updates remembered state, and a
	// RemainingMatchingIssues response to it is recorded as ThisItem.
	// nullopt = cancellation overrode the decision.
	[[nodiscard]] std::optional<Decision> resolveDecision(OperationIssue issue, bool remainingMatchingScopeAllowed = true);

	void publishProgress(const ProgressSnapshot& snapshot);

	// Bounded accumulation: every call counts, only the first few diagnostics are retained verbatim.
	void recordWarning(OperationDiagnostic diagnostic);
	void recordFailure(OperationDiagnostic diagnostic);

	void addCompletedItem() noexcept { ++_accumulatedSummary.completedItems; }
	void addSkippedItem() noexcept { ++_accumulatedSummary.skippedItems; }
	void addAlreadySatisfiedItem() noexcept { ++_accumulatedSummary.alreadySatisfiedItems; }
	void addTransferredBytes(const uint64_t bytes) noexcept { _accumulatedSummary.transferredBytes += bytes; }

	// Failed items are counted by recordFailure: every originating failed item records exactly one
	// terminal diagnostic, and propagated parent outcomes record none.
	[[nodiscard]] OperationSummary makeSummary(CompletionStatus status) const;

private:
	CheckpointCallback _checkpoint;
	DecisionCallback _decisionProvider;
	ProgressCallback _progressPublisher;

	// Keyed by IssueKind; only actions rememberable per the normative table are ever stored.
	std::array<std::optional<DecisionAction>, 6> _rememberedDecisions;

	OperationSummary _accumulatedSummary;
};

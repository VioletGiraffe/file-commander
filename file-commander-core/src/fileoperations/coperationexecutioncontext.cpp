#include "coperationexecutioncontext.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

#include <algorithm>

namespace
{

// Enough for the completion dialog; the aggregate counts stay exact regardless.
constexpr size_t maxRepresentativeDiagnostics = 16;

} // namespace

COperationExecutionContext::COperationExecutionContext(CheckpointCallback checkpoint, DecisionCallback decisionProvider, ProgressCallback progressPublisher)
	: _checkpoint{ mv(checkpoint) }
	, _decisionProvider{ mv(decisionProvider) }
	, _progressPublisher{ mv(progressPublisher) }
{
	assert_r(_checkpoint && _decisionProvider && _progressPublisher);
}

bool COperationExecutionContext::checkpoint()
{
	return _checkpoint();
}

std::optional<Decision> COperationExecutionContext::resolveDecision(OperationIssue issue, const bool remainingMatchingScopeAllowed)
{
	const auto kindIndex = static_cast<size_t>(issue.kind);
	assert_debug_only(kindIndex < _rememberedDecisions.size());

	if (remainingMatchingScopeAllowed)
	{
		if (const auto& remembered = _rememberedDecisions[kindIndex]; remembered)
			return Decision{ *remembered, DecisionScope::RemainingMatchingIssues };
	}

	const IssueKind kind = issue.kind;
	const DecisionRequest request{ mv(issue), allowedActionsFor(kind), remainingMatchingScopeAllowed };
	auto decision = _decisionProvider(request);
	if (!decision)
		return {};

	assert_r(std::find(request.allowedActions.begin(), request.allowedActions.end(), decision->action) != request.allowedActions.end());

	if (decision->scope == DecisionScope::RemainingMatchingIssues)
	{
		if (remainingMatchingScopeAllowed && isActionRememberable(kind, decision->action))
			_rememberedDecisions[kindIndex] = decision->action;
		else
			decision->scope = DecisionScope::ThisItem; // An All response is void here; the action still answers this item
	}

	return decision;
}

void COperationExecutionContext::publishProgress(const ProgressSnapshot& snapshot)
{
	_progressPublisher(snapshot);
}

void COperationExecutionContext::recordWarning(OperationDiagnostic diagnostic)
{
	++_accumulatedSummary.warningCount;
	if (_accumulatedSummary.representativeWarnings.size() < maxRepresentativeDiagnostics)
		_accumulatedSummary.representativeWarnings.push_back(mv(diagnostic));
}

void COperationExecutionContext::recordFailure(OperationDiagnostic diagnostic)
{
	++_accumulatedSummary.failedItems;
	if (_accumulatedSummary.representativeFailures.size() < maxRepresentativeDiagnostics)
		_accumulatedSummary.representativeFailures.push_back(mv(diagnostic));
}

OperationSummary COperationExecutionContext::makeSummary(const CompletionStatus status) const
{
	OperationSummary summary = _accumulatedSummary;
	summary.status = status;
	return summary;
}

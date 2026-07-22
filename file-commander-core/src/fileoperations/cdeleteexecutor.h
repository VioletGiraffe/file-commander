#pragma once

#include "csourcetreebuilder.h"

class COperationExecutionContext;

// The synchronous post-order permanent-delete executor. Runs entirely on the caller's thread; all
// environment interaction - checkpoints, decisions, progress, diagnostics - goes through the execution context.
class CDeleteExecutor
{
public:
	explicit CDeleteExecutor(COperationExecutionContext& context) noexcept;

	[[nodiscard]] OperationSummary run(const PermanentDeleteRequest& request);

private:
	[[nodiscard]] NodeOutcome deleteRoot(const CEntryPath& source);

	// Fresh root inspection with the local ActionFailed retry policy. An absent root is already satisfied,
	// not an error: absence is this operation's goal.
	[[nodiscard]] std::variant<EntrySnapshot, NodeOutcome> inspectSourceRoot(const CEntryPath& source);

	// Post-order recursion: children first; a parent with remaining skipped/incomplete content is
	// preserved and reports the aggregate outcome.
	[[nodiscard]] NodeOutcome deleteNode(const SourceNode& node);

	// One entry's removal with the local retry policy: the read-only preflight and reactive
	// reclassification for non-link regular files, MakeWritable remediation, Retry/Skip/Cancel.
	// Links and directories are removed as themselves and never remediated. Pre-commit semantics;
	// WP7's committed move cleanup adds the phase distinction when its call site exists.
	[[nodiscard]] NodeOutcome removeEntryWithPolicy(const EntrySnapshot& entry);

	[[nodiscard]] NodeOutcome recordEntrySkipped();

	// A root's contribution to the aggregate item total; publishes the exact total once every root's is known.
	void rootTotalsResolved(size_t items);

	COperationExecutionContext& _context;

	size_t _rootsWithUnresolvedTotals = 0;
	size_t _knownTotalItems = 0;
};

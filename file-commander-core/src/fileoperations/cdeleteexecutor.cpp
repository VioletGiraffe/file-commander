#include "cdeleteexecutor.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

CDeleteExecutor::CDeleteExecutor(COperationExecutionContext& context) noexcept
	: _context{ context }
{
}

OperationSummary CDeleteExecutor::run(const PermanentDeleteRequest& request)
{
	_rootsWithUnresolvedTotals = request.sources.size();

	bool anyCancelled = false;
	bool anyFailed = false;
	for (const CEntryPath& source : request.sources)
	{
		const NodeOutcome outcome = deleteRoot(source);
		if (outcome == NodeOutcome::Cancelled)
		{
			anyCancelled = true;
			break; // Cancellation stops further traversal; remaining roots stay untouched
		}
		anyFailed = anyFailed || outcome == NodeOutcome::Failed;
	}

	const CompletionStatus status = anyCancelled ? CompletionStatus::Cancelled
		: anyFailed ? CompletionStatus::Failed : CompletionStatus::Completed;
	return _context.makeSummary(status);
}

std::variant<EntrySnapshot, NodeOutcome> CDeleteExecutor::inspectSourceRoot(const CEntryPath& source)
{
	for (;;)
	{
		auto inspected = inspectEntry(source);
		if (inspected && inspected->has_value())
			return mv(**inspected);

		if (inspected)
		{
			// Nothing at the path: the desired end state already holds.
			_context.addAlreadySatisfiedItems(1);
			return NodeOutcome::AlreadySatisfied;
		}

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed,
			EntrySnapshot{ source, OperationEntryKind::RegularFile, 0 }, {}, FailureDetails{ FailedAction::InspectSource, mv(inspected.error()) } });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
		{
			_context.addSkippedItems(1);
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CDeleteExecutor::deleteRoot(const CEntryPath& source)
{
	if (!_context.checkpoint())
	{
		rootTotalsResolved(0);
		return NodeOutcome::Cancelled;
	}

	auto rootEntry = inspectSourceRoot(source);
	if (const auto* endedOutcome = std::get_if<NodeOutcome>(&rootEntry))
	{
		rootTotalsResolved(0);
		return *endedOutcome;
	}
	EntrySnapshot& root = std::get<EntrySnapshot>(rootEntry);

	// Build the manifest with the local retry policy; the diagnostic names the entry that failed to scan.
	// Leaf roots (files, links, Other) come back as single-node manifests without any listing.
	std::optional<SourceNode> tree;
	while (!tree)
	{
		auto built = buildSourceTree(_context, root, SourceTreeBuildMode::PermanentDelete);
		if (auto* node = std::get_if<SourceNode>(&built))
		{
			tree = mv(*node);
			break;
		}
		if (std::holds_alternative<ScanCancelled>(built))
		{
			rootTotalsResolved(0);
			return NodeOutcome::Cancelled;
		}

		auto& diagnostic = std::get<OperationDiagnostic>(built);
		if (diagnostic.failure.filesystemError.category == FileErrorCategory::NotFound && diagnostic.source.path == root.path)
		{
			// The root itself vanished during the scan: the desired end state already holds. (A vanished
			// subdirectory needs no such case: on Retry the fresh scan omits it silently.)
			_context.addAlreadySatisfiedItems(1);
			rootTotalsResolved(0);
			return NodeOutcome::AlreadySatisfied;
		}

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, mv(diagnostic.source), {}, mv(diagnostic.failure) });
		if (!decision || decision->action == DecisionAction::Cancel)
		{
			rootTotalsResolved(0);
			return NodeOutcome::Cancelled;
		}
		if (decision->action == DecisionAction::Skip)
		{
			_context.addSkippedItems(1); // Unscanned: only the root entry itself is known
			rootTotalsResolved(0);
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry);
	}

	rootTotalsResolved(tree->subtreeItems);
	_context.publishProgressSnapshot(); // Back to Working, with the item total possibly exact from here on
	return deleteNode(*tree);
}

NodeOutcome CDeleteExecutor::deleteNode(const SourceNode& node)
{
	if (!_context.checkpoint())
		return NodeOutcome::Cancelled;

	NodeOutcome aggregate = NodeOutcome::Completed;
	for (const SourceNode& child : node.children)
	{
		const NodeOutcome childOutcome = deleteNode(child);
		aggregate = aggregateChildOutcome(aggregate, childOutcome);
		if (childOutcome == NodeOutcome::Cancelled)
			break;
	}

	// Skipped/incomplete content remains below (or cancellation won): preserve this directory.
	if (aggregate != NodeOutcome::Completed)
		return aggregate;

	return removeEntryWithPolicy(node.entry);
}

NodeOutcome CDeleteExecutor::removeEntryWithPolicy(const EntrySnapshot& entry)
{
	const bool remediableFile = entry.kind == OperationEntryKind::RegularFile;
	bool makeWritableAuthorized = false;

	_context.progress().setCurrentEntry(entry.path, {});
	_context.publishProgressSnapshot();

	for (;;)
	{
		if (!_context.checkpoint())
			return NodeOutcome::Cancelled;

		// Read-only preflight. Inspection trouble is deliberately not prompted here: the removal attempt
		// below produces the truthful failure, or succeeds outright.
		if (remediableFile && !makeWritableAuthorized)
		{
			if (const auto writable = isEntryWritableNoFollow(entry); writable && !*writable)
			{
				const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ReadOnlySourceRemoval, entry, {}, {} });
				if (!decision || decision->action == DecisionAction::Cancel)
					return NodeOutcome::Cancelled;
				if (decision->action == DecisionAction::Skip)
					return recordEntrySkipped();
				if (decision->action == DecisionAction::MakeWritable)
					makeWritableAuthorized = true; // Remediation follows within this same attempt
				else
				{
					assert_debug_only(decision->action == DecisionAction::Retry);
					continue; // No mutation; fresh inspection can observe an external permission change
				}
			}
		}

		if (makeWritableAuthorized)
		{
			if (const auto madeWritable = CFileSystemMutator::setEntryWritable(entry, true); !madeWritable)
			{
				const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, entry, {},
					FailureDetails{ FailedAction::MakeWritable, madeWritable.error() } });
				if (!decision || decision->action == DecisionAction::Cancel)
					return NodeOutcome::Cancelled;
				if (decision->action == DecisionAction::Skip)
					return recordEntrySkipped();
				assert_debug_only(decision->action == DecisionAction::Retry);
				continue;
			}
		}

		auto removed = CFileSystemMutator::removeEntry(entry);
		if (removed)
		{
			_context.addCompletedItem();
			_context.progress().itemCompleted();
			_context.progress().clearCurrentEntry();
			_context.publishProgressSnapshot();
			return NodeOutcome::Completed;
		}

		CFileSystemError error = mv(removed.error());
		if (error.category == FileErrorCategory::NotFound)
		{
			// Vanished since the scan: the desired end state already holds.
			_context.addAlreadySatisfiedItems(1);
			_context.progress().advanceWithoutTransfer(0, 1);
			_context.progress().clearCurrentEntry();
			_context.publishProgressSnapshot();
			return NodeOutcome::AlreadySatisfied;
		}

		// A raced read-only result returns to the read-only question only when fresh inspection confirms
		// it. Directories and links never enter remediation, and generic access denied is never guessed
		// to mean read-only - only the unambiguous native category qualifies.
		if (remediableFile && error.category == FileErrorCategory::ReadOnly)
		{
			if (const auto writable = isEntryWritableNoFollow(entry); writable && !*writable)
			{
				const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ReadOnlySourceRemoval, entry, {},
					FailureDetails{ FailedAction::RemoveEntry, mv(error) } });
				if (!decision || decision->action == DecisionAction::Cancel)
					return NodeOutcome::Cancelled;
				if (decision->action == DecisionAction::Skip)
					return recordEntrySkipped();
				if (decision->action == DecisionAction::MakeWritable)
					makeWritableAuthorized = true;
				else
					assert_debug_only(decision->action == DecisionAction::Retry);
				continue;
			}
		}

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, entry, {},
			FailureDetails{ FailedAction::RemoveEntry, mv(error) } });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
			return recordEntrySkipped();
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CDeleteExecutor::recordEntrySkipped()
{
	_context.addSkippedItems(1);
	_context.progress().advanceWithoutTransfer(0, 1);
	_context.progress().clearCurrentEntry();
	_context.publishProgressSnapshot();
	return NodeOutcome::Skipped;
}

void CDeleteExecutor::rootTotalsResolved(const size_t items)
{
	assert_debug_only(_rootsWithUnresolvedTotals > 0);
	_knownTotalItems += items;
	if (--_rootsWithUnresolvedTotals == 0)
		_context.progress().setTotals(0, _knownTotalItems); // A delete moves no bytes; the byte total is exactly zero
}

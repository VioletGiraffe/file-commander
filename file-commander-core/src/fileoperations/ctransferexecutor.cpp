#include "ctransferexecutor.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"
#include "cstagedfilecopy.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

namespace
{

// Diagnostic context for committed-cleanup prompts and records: the published destination entry.
EntrySnapshot destinationSnapshotFor(const EntrySnapshot& source, const CEntryPath& destination)
{
	const bool directoryLike = source.kind == OperationEntryKind::Directory || source.kind == OperationEntryKind::DirectoryLink;
	return EntrySnapshot{ destination, directoryLike ? OperationEntryKind::Directory : OperationEntryKind::RegularFile, source.size };
}

} // namespace

CTransferExecutor::CTransferExecutor(COperationExecutionContext& context, const uint64_t transferChunkSize) noexcept
	: _context{ context }
	, _transferChunkSize{ transferChunkSize }
{
	assert_debug_only(transferChunkSize > 0);
}

OperationSummary CTransferExecutor::run(const TransferRequest& request)
{
	_requestKind = request.kind;

	const auto intents = rootTransferIntents(request);
	_rootsWithUnresolvedTotals = intents.size();

	bool anyCancelled = false;
	bool anyFailed = false;
	for (const RootTransferIntent& intent : intents)
	{
		const NodeOutcome outcome = _requestKind == TransferKind::Move ? moveRoot(intent) : copyRoot(intent);
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

std::variant<EntrySnapshot, NodeOutcome> CTransferExecutor::inspectSourceRoot(const CEntryPath& source)
{
	for (;;)
	{
		auto inspected = inspectEntry(source);
		if (inspected && inspected->has_value())
			return mv(**inspected);

		CFileSystemError error = inspected
			? CFileSystemError{ FileErrorCategory::NotFound, 0, QStringLiteral("The source no longer exists") }
			: mv(inspected.error());

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed,
			EntrySnapshot{ source, OperationEntryKind::RegularFile, 0 }, {}, FailureDetails{ FailedAction::InspectSource, mv(error) } });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
			return NodeOutcome::Skipped;
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

std::variant<SourceNode, NodeOutcome> CTransferExecutor::buildManifestWithRetry(const EntrySnapshot& root)
{
	// The diagnostic names the entry that failed to scan, not necessarily the root.
	for (;;)
	{
		auto built = buildSourceTree(_context, root, SourceTreeBuildMode::MaterializingTransfer);
		if (auto* node = std::get_if<SourceNode>(&built))
			return mv(*node);
		if (std::holds_alternative<ScanCancelled>(built))
			return NodeOutcome::Cancelled;

		auto& diagnostic = std::get<OperationDiagnostic>(built);
		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, mv(diagnostic.source), {}, mv(diagnostic.failure) });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
			return NodeOutcome::Skipped;
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CTransferExecutor::finishUnscannedRoot(const NodeOutcome outcome)
{
	switch (outcome)
	{
	case NodeOutcome::Completed: // Renamed wholesale; totals resolve first so the published snapshot carries them when this was the last root
		_context.addCompletedItems(1);
		_context.progress().itemCompleted();
		rootTotalsResolved(0, 1);
		_context.publishProgressSnapshot();
		break;
	case NodeOutcome::Skipped:
		_context.addSkippedItems(1);
		rootTotalsResolved(0, 0);
		break;
	case NodeOutcome::AlreadySatisfied:
		_context.addAlreadySatisfiedItems(1);
		rootTotalsResolved(0, 0);
		break;
	default:
		assert_debug_only(outcome == NodeOutcome::Cancelled);
		rootTotalsResolved(0, 0);
		break;
	}
	return outcome;
}

// --- Copy ---

NodeOutcome CTransferExecutor::copyRoot(const RootTransferIntent& intent)
{
	if (!_context.checkpoint())
		return finishUnscannedRoot(NodeOutcome::Cancelled);

	auto rootEntry = inspectSourceRoot(intent.source);
	if (const auto* endedOutcome = std::get_if<NodeOutcome>(&rootEntry))
		return finishUnscannedRoot(*endedOutcome);
	EntrySnapshot& root = std::get<EntrySnapshot>(rootEntry);

	const bool directoryLike = root.kind == OperationEntryKind::Directory || root.kind == OperationEntryKind::DirectoryLink;
	if (!directoryLike)
	{
		// A single-entry manifest; Other and file kinds route through the ordinary node dispatch.
		SourceNode leaf{ .entry = mv(root) };
		leaf.subtreeBytes = leaf.entry.size;
		leaf.subtreeItems = 1;
		rootTotalsResolved(leaf.subtreeBytes, 1);
		return copyNode(leaf, intent.proposedDestination, TransferNodePosition::SelectedRoot);
	}

	// Root-first resolution: a root skipped at its collision is never scanned.
	DestinationChoice choice = resolveDirectoryDestination(_context, root, intent.proposedDestination, TransferNodePosition::SelectedRoot);
	if (std::holds_alternative<SkipNode>(choice))
		return finishUnscannedRoot(NodeOutcome::Skipped);
	if (std::holds_alternative<CancelOperation>(choice))
		return finishUnscannedRoot(NodeOutcome::Cancelled);
	if (std::holds_alternative<AlreadySatisfied>(choice))
		return finishUnscannedRoot(NodeOutcome::AlreadySatisfied);

	auto built = buildManifestWithRetry(root);
	if (const auto* endedOutcome = std::get_if<NodeOutcome>(&built))
		return finishUnscannedRoot(*endedOutcome);
	SourceNode& tree = std::get<SourceNode>(built);

	rootTotalsResolved(tree.subtreeBytes, tree.subtreeItems);
	_context.publishProgressSnapshot(); // Back to Working, with totals possibly exact from here on
	return runDirectoryNode(tree, TransferNodePosition::SelectedRoot, mv(choice), false);
}

NodeOutcome CTransferExecutor::copyNode(const SourceNode& node, CEntryPath proposedDestination, const TransferNodePosition position)
{
	if (!_context.checkpoint())
		return NodeOutcome::Cancelled;

	if (node.entry.kind == OperationEntryKind::Other)
	{
		// Never streamed as a file; the one policy question is skip or cancel.
		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::UnsupportedEntry, node.entry, {}, {} });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		assert_debug_only(decision->action == DecisionAction::Skip);
		accountSkippedSubtree(node);
		return NodeOutcome::Skipped;
	}

	const bool fileLike = node.entry.kind == OperationEntryKind::RegularFile || node.entry.kind == OperationEntryKind::FileLink;
	if (!fileLike)
		return runDirectoryNode(node, position, resolveDirectoryDestination(_context, node.entry, mv(proposedDestination), position), false);

	for (;;)
	{
		DestinationChoice choice = resolveFileDestination(_context, node.entry, mv(proposedDestination));
		if (std::holds_alternative<SkipNode>(choice))
		{
			accountSkippedSubtree(node);
			return NodeOutcome::Skipped;
		}
		if (std::holds_alternative<CancelOperation>(choice))
			return NodeOutcome::Cancelled;
		if (std::holds_alternative<AlreadySatisfied>(choice))
		{
			accountAlreadySatisfiedSubtree(node);
			return NodeOutcome::AlreadySatisfied;
		}

		auto& use = std::get<UseDestination>(choice); // MergeDirectory cannot come from the file resolver
		const auto outcome = stagedFileTransferWithPolicy(node.entry, use.path, use.replacement, PublishedSourceAction::RetainSource, false);
		if (outcome)
			return *outcome;
		proposedDestination = mv(use.path); // A new collision appeared at publication: resolve it freshly
	}
}

NodeOutcome CTransferExecutor::runDirectoryNode(const SourceNode& node, const TransferNodePosition position, DestinationChoice choice, const bool knownCrossDevice)
{
	for (;;)
	{
		if (std::holds_alternative<SkipNode>(choice))
		{
			accountSkippedSubtree(node);
			return NodeOutcome::Skipped;
		}
		if (std::holds_alternative<CancelOperation>(choice))
			return NodeOutcome::Cancelled;
		if (std::holds_alternative<AlreadySatisfied>(choice))
		{
			accountAlreadySatisfiedSubtree(node);
			return NodeOutcome::AlreadySatisfied;
		}
		if (const auto* merge = std::get_if<MergeDirectory>(&choice))
		{
			return _requestKind == TransferKind::Move
				? moveDirectoryContents(node, merge->path, false, knownCrossDevice)
				: copyDirectoryContents(node, merge->path, false);
		}

		const auto& use = std::get<UseDestination>(choice);
		const auto created = CFileSystemMutator::createDirectories(use.path);
		if (created)
		{
			if (*created == DirectoryCreationOutcome::CreatedFinalDirectory)
			{
				return _requestKind == TransferKind::Move
					? moveDirectoryContents(node, use.path, true, knownCrossDevice)
					: copyDirectoryContents(node, use.path, true);
			}

			// An entry appeared since resolution: a fresh collision, resolved anew.
			choice = resolveDirectoryDestination(_context, node.entry, use.path, position);
			continue;
		}

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, node.entry,
			{}, FailureDetails{ FailedAction::CreateDestinationDirectory, created.error() } });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
		{
			accountSkippedSubtree(node);
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CTransferExecutor::copyDirectoryContents(const SourceNode& node, const CEntryPath& destination, const bool operationCreated)
{
	_context.addCompletedItems(1); // The directory entry itself: created or merged successfully
	_context.progress().itemCompleted();
	_context.publishProgressSnapshot();

	// Only an operation-created directory is stamped; a pre-existing merge target keeps its own times.
	std::optional<CopyableDirectoryTimes> copyableTimes;
	if (operationCreated)
	{
		auto captured = readCopyableDirectoryTimes(node.entry.path);
		if (captured)
			copyableTimes = *captured;
		else
			recordTimestampWarning(node.entry, destination, mv(captured.error()));
	}

	NodeOutcome aggregate = NodeOutcome::Completed;
	for (const SourceNode& child : node.children)
	{
		const NodeOutcome childOutcome = copyNode(child, destination.child(child.entry.path.name()), TransferNodePosition::Descendant);
		aggregate = aggregateChildOutcome(aggregate, childOutcome);
		if (childOutcome == NodeOutcome::Cancelled)
			break;
	}

	// Post-order, so child mutations cannot disturb the stamp; skipped on cancellation because the
	// directory's content story did not finish.
	if (operationCreated && copyableTimes && aggregate != NodeOutcome::Cancelled)
	{
		if (const auto applied = CFileSystemMutator::applyDirectoryTimes(destination, *copyableTimes); !applied)
			recordTimestampWarning(node.entry, destination, applied.error());
	}

	return aggregate;
}

// --- Move ---

NodeOutcome CTransferExecutor::moveRoot(const RootTransferIntent& intent)
{
	if (!_context.checkpoint())
		return finishUnscannedRoot(NodeOutcome::Cancelled);

	auto rootEntry = inspectSourceRoot(intent.source);
	if (const auto* endedOutcome = std::get_if<NodeOutcome>(&rootEntry))
		return finishUnscannedRoot(*endedOutcome);
	EntrySnapshot& root = std::get<EntrySnapshot>(rootEntry);

	// Optimistic whole-root rename before resolution and scanning. RequireAbsent cannot clobber anything,
	// and a case-only respell on a case-insensitive filesystem succeeds right here through the primitive's
	// fallback - which is why resolution's silent same-object handling never swallows one.
	auto renamed = renameEntryWithPolicy(root, intent.proposedDestination, ReplacementMode::RequireAbsent);
	if (const auto* outcome = std::get_if<NodeOutcome>(&renamed))
		return finishUnscannedRoot(*outcome);
	bool knownCrossDevice = std::get<RenameBlock>(renamed) == RenameBlock::CrossDevice;

	const bool directoryLike = root.kind == OperationEntryKind::Directory || root.kind == OperationEntryKind::DirectoryLink;
	if (!directoryLike)
	{
		SourceNode leaf{ .entry = mv(root) };
		leaf.subtreeBytes = leaf.entry.size;
		leaf.subtreeItems = 1;
		rootTotalsResolved(leaf.subtreeBytes, 1);
		return moveNode(leaf, intent.proposedDestination, TransferNodePosition::SelectedRoot, knownCrossDevice);
	}

	// Root-first resolution; a rename decision loops back so the respelled target gets its own rename attempt.
	CEntryPath proposedDestination = intent.proposedDestination;
	for (;;)
	{
		DestinationChoice choice = resolveDirectoryDestination(_context, root, proposedDestination, TransferNodePosition::SelectedRoot);
		if (std::holds_alternative<SkipNode>(choice))
			return finishUnscannedRoot(NodeOutcome::Skipped);
		if (std::holds_alternative<CancelOperation>(choice))
			return finishUnscannedRoot(NodeOutcome::Cancelled);
		if (std::holds_alternative<AlreadySatisfied>(choice))
			return finishUnscannedRoot(NodeOutcome::AlreadySatisfied);

		if (const auto* use = std::get_if<UseDestination>(&choice); use && !knownCrossDevice)
		{
			auto renamedToResolved = renameEntryWithPolicy(root, use->path, use->replacement);
			if (const auto* outcome = std::get_if<NodeOutcome>(&renamedToResolved))
				return finishUnscannedRoot(*outcome);
			if (std::get<RenameBlock>(renamedToResolved) == RenameBlock::CrossDevice)
				knownCrossDevice = true;
			else
			{
				proposedDestination = use->path; // An entry raced in: resolve the new collision freshly
				continue;
			}
		}

		// Merge, or cross-device materialization: traversal is required from here.
		auto built = buildManifestWithRetry(root);
		if (const auto* endedOutcome = std::get_if<NodeOutcome>(&built))
			return finishUnscannedRoot(*endedOutcome);
		SourceNode& tree = std::get<SourceNode>(built);

		rootTotalsResolved(tree.subtreeBytes, tree.subtreeItems);
		_context.publishProgressSnapshot(); // Back to Working, with totals possibly exact from here on
		return runDirectoryNode(tree, TransferNodePosition::SelectedRoot, mv(choice), knownCrossDevice);
	}
}

NodeOutcome CTransferExecutor::moveNode(const SourceNode& node, CEntryPath proposedDestination, const TransferNodePosition position, bool knownCrossDevice)
{
	if (!_context.checkpoint())
		return NodeOutcome::Cancelled;

	const bool owned = node.ownership == SourceOwnership::Owned;

	if (owned && !knownCrossDevice)
	{
		// Rename-first at this boundary; borrowed entries are materialized, never renamed away from their source.
		auto renamed = renameEntryWithPolicy(node.entry, proposedDestination, ReplacementMode::RequireAbsent);
		if (const auto* outcome = std::get_if<NodeOutcome>(&renamed))
		{
			if (*outcome == NodeOutcome::Completed)
				accountRenamedSubtree(node);
			else if (*outcome == NodeOutcome::Skipped)
				accountSkippedSubtree(node);
			return *outcome;
		}
		knownCrossDevice = std::get<RenameBlock>(renamed) == RenameBlock::CrossDevice;
		// DestinationOccupied falls through to resolution
	}

	if (node.entry.kind == OperationEntryKind::Other)
	{
		// With rename unavailable this entry cannot move - it is never streamed as a file.
		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::UnsupportedEntry, node.entry, {}, {} });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		assert_debug_only(decision->action == DecisionAction::Skip);
		accountSkippedSubtree(node);
		return NodeOutcome::Skipped;
	}

	const bool fileLike = node.entry.kind == OperationEntryKind::RegularFile || node.entry.kind == OperationEntryKind::FileLink;
	if (!fileLike)
		return runDirectoryNode(node, position, resolveDirectoryDestination(_context, node.entry, mv(proposedDestination), position), knownCrossDevice);

	return moveFileNode(node, mv(proposedDestination), knownCrossDevice);
}

NodeOutcome CTransferExecutor::moveFileNode(const SourceNode& node, CEntryPath proposedDestination, bool knownCrossDevice)
{
	const bool owned = node.ownership == SourceOwnership::Owned;
	std::optional<bool> makeWritableAuthorized; // The pre-publication read-only policy answer, resolved once per node

	for (;;)
	{
		DestinationChoice choice = resolveFileDestination(_context, node.entry, mv(proposedDestination));
		if (std::holds_alternative<SkipNode>(choice))
		{
			accountSkippedSubtree(node);
			return NodeOutcome::Skipped;
		}
		if (std::holds_alternative<CancelOperation>(choice))
			return NodeOutcome::Cancelled;
		if (std::holds_alternative<AlreadySatisfied>(choice))
		{
			// Same object: move never removes a source that is also the destination.
			accountAlreadySatisfiedSubtree(node);
			return NodeOutcome::AlreadySatisfied;
		}

		auto& use = std::get<UseDestination>(choice); // MergeDirectory cannot come from the file resolver

		if (owned && !knownCrossDevice)
		{
			// The resolved disposition can still be one atomic native rename: an authorized replacement,
			// or a respelled target from a Rename decision.
			auto renamed = renameEntryWithPolicy(node.entry, use.path, use.replacement);
			if (const auto* outcome = std::get_if<NodeOutcome>(&renamed))
			{
				if (*outcome == NodeOutcome::Completed)
					accountRenamedSubtree(node);
				else if (*outcome == NodeOutcome::Skipped)
					accountSkippedSubtree(node);
				return *outcome;
			}
			if (std::get<RenameBlock>(renamed) == RenameBlock::CrossDevice)
				knownCrossDevice = true;
			else
			{
				proposedDestination = mv(use.path); // The destination raced: resolve the new collision freshly
				continue;
			}
		}

		if (owned && node.entry.kind == OperationEntryKind::RegularFile && !makeWritableAuthorized.has_value())
		{
			auto preflight = resolveMoveReadOnlyPreflight(node.entry);
			if (const auto* outcome = std::get_if<NodeOutcome>(&preflight))
			{
				if (*outcome == NodeOutcome::Skipped)
					accountSkippedSubtree(node);
				return *outcome;
			}
			makeWritableAuthorized = std::get<bool>(preflight);
		}

		const auto sourceAction = owned ? PublishedSourceAction::RemoveOwnedSource : PublishedSourceAction::RetainSource;
		const auto outcome = stagedFileTransferWithPolicy(node.entry, use.path, use.replacement, sourceAction, makeWritableAuthorized.value_or(false));
		if (outcome)
			return *outcome;
		proposedDestination = mv(use.path); // A new collision appeared at publication: resolve it freshly
	}
}

NodeOutcome CTransferExecutor::moveDirectoryContents(const SourceNode& node, const CEntryPath& destination, const bool operationCreated, const bool knownCrossDevice)
{
	const bool owned = node.ownership == SourceOwnership::Owned;
	if (!owned)
	{
		// Materialization is a borrowed entry's complete required work; an owned directory counts only
		// after its source entry is removed.
		_context.addCompletedItems(1);
		_context.progress().itemCompleted();
		_context.publishProgressSnapshot();
	}

	// Only an operation-created directory is stamped; a pre-existing merge target keeps its own times.
	// For a materialized directory link, the capture deliberately follows the link.
	std::optional<CopyableDirectoryTimes> copyableTimes;
	if (operationCreated)
	{
		auto captured = readCopyableDirectoryTimes(node.entry.path);
		if (captured)
			copyableTimes = *captured;
		else
			recordTimestampWarning(node.entry, destination, mv(captured.error()));
	}

	NodeOutcome aggregate = NodeOutcome::Completed;
	bool allChildrenCompleted = true;
	for (const SourceNode& child : node.children)
	{
		const NodeOutcome childOutcome = moveNode(child, destination.child(child.entry.path.name()), TransferNodePosition::Descendant, knownCrossDevice);
		aggregate = aggregateChildOutcome(aggregate, childOutcome);
		allChildrenCompleted = allChildrenCompleted && childOutcome == NodeOutcome::Completed;
		if (childOutcome == NodeOutcome::Cancelled)
			break;
	}

	if (operationCreated && copyableTimes && aggregate != NodeOutcome::Cancelled)
	{
		if (const auto applied = CFileSystemMutator::applyDirectoryTimes(destination, *copyableTimes); !applied)
			recordTimestampWarning(node.entry, destination, applied.error());
	}

	if (!owned || aggregate == NodeOutcome::Cancelled)
		return aggregate;

	// An AlreadySatisfied child keeps its source entry in place, so it blocks cleanup exactly like a skip.
	if (!allChildrenCompleted)
		return aggregate == NodeOutcome::Completed ? NodeOutcome::Partial : aggregate;

	// Committed cleanup of the emptied source directory (or the directory-link entry); timestamps are
	// finalized above, and no cancellation checkpoint separates them from this removal.
	return removePublishedSourceWithPolicy(node.entry, destination, false);
}

std::variant<NodeOutcome, CTransferExecutor::RenameBlock> CTransferExecutor::renameEntryWithPolicy(
	const EntrySnapshot& source, const CEntryPath& destination, const ReplacementMode replacement)
{
	for (;;)
	{
		auto renamed = CFileSystemMutator::renameEntry(source.path, destination, replacement);
		if (renamed)
			return NodeOutcome::Completed;

		CFileSystemError error = mv(renamed.error());
		if (error.category == FileErrorCategory::CrossDevice)
			return RenameBlock::CrossDevice;
		if (error.category == FileErrorCategory::AlreadyExists)
			return RenameBlock::DestinationOccupied;

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, source, {},
			FailureDetails{ FailedAction::RenameEntry, mv(error) } });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
			return NodeOutcome::Skipped;
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

std::variant<bool, NodeOutcome> CTransferExecutor::resolveMoveReadOnlyPreflight(const EntrySnapshot& entry)
{
	for (;;)
	{
		// Inspection trouble is deliberately not prompted here: the staged transfer surfaces the truthful failure.
		const auto writable = isEntryWritableNoFollow(entry);
		if (!writable || *writable)
			return false;

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ReadOnlySourceRemoval, entry, {}, {} });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
			return NodeOutcome::Skipped;
		if (decision->action == DecisionAction::MakeWritable)
			return true; // A local authorization only; the source is not touched until after publication
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CTransferExecutor::removePublishedSourceWithPolicy(const EntrySnapshot& entry, const CEntryPath& destination, const bool makeWritableAuthorized)
{
	const bool remediableFile = entry.kind == OperationEntryKind::RegularFile;
	assert_debug_only(!makeWritableAuthorized || remediableFile); // The preflight only ever authorizes a regular file
	bool applyWritableAuthorization = makeWritableAuthorized;

	for (;;)
	{
		if (applyWritableAuthorization)
		{
			if (const auto madeWritable = CFileSystemMutator::setEntryWritable(entry, true); !madeWritable)
			{
				FailureDetails failure{ FailedAction::MakeWritable, madeWritable.error() };
				const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, entry,
					destinationSnapshotFor(entry, destination), failure }, false);
				if (!decision || decision->action == DecisionAction::Cancel)
					return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Cancelled);
				if (decision->action == DecisionAction::Skip)
					return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Failed);
				assert_debug_only(decision->action == DecisionAction::Retry);
				continue;
			}
			applyWritableAuthorization = false; // Applied; a removal retry must not redo it
		}

		auto removed = CFileSystemMutator::removeEntry(entry);
		if (removed)
			break;

		CFileSystemError error = mv(removed.error());
		if (error.category == FileErrorCategory::NotFound)
			break; // The source is gone - the required end state

		// A raced read-only result returns to the read-only question only when fresh inspection confirms
		// it; directories and links never enter remediation, and generic access denied never means read-only.
		if (remediableFile && error.category == FileErrorCategory::ReadOnly)
		{
			if (const auto writable = isEntryWritableNoFollow(entry); writable && !*writable)
			{
				FailureDetails failure{ FailedAction::RemovePublishedMoveSource, mv(error) };
				const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ReadOnlySourceRemoval, entry,
					destinationSnapshotFor(entry, destination), failure }, false);
				if (!decision || decision->action == DecisionAction::Cancel)
					return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Cancelled);
				if (decision->action == DecisionAction::Skip)
					return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Failed);
				if (decision->action == DecisionAction::MakeWritable)
					applyWritableAuthorization = true;
				else
					assert_debug_only(decision->action == DecisionAction::Retry);
				continue;
			}
		}

		FailureDetails failure{ FailedAction::RemovePublishedMoveSource, mv(error) };
		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, entry,
			destinationSnapshotFor(entry, destination), failure }, false);
		if (!decision || decision->action == DecisionAction::Cancel)
			return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Cancelled);
		if (decision->action == DecisionAction::Skip)
			return recordTerminalCleanupFailure(entry, destination, mv(failure), NodeOutcome::Failed);
		assert_debug_only(decision->action == DecisionAction::Retry);
	}

	_context.addCompletedItems(1); // Moved: required source removal is done
	_context.progress().itemCompleted();
	_context.progress().clearCurrentEntry();
	_context.publishProgressSnapshot();
	return NodeOutcome::Completed;
}

NodeOutcome CTransferExecutor::recordTerminalCleanupFailure(const EntrySnapshot& entry, const CEntryPath& destination,
	FailureDetails failure, const NodeOutcome outcome)
{
	// Both entries are retained; the published destination is never deleted as rollback.
	_context.recordFailure(OperationDiagnostic{ mv(failure), entry, destinationSnapshotFor(entry, destination) });
	_context.progress().advanceWithoutTransfer(0, 1);
	_context.progress().clearCurrentEntry();
	_context.publishProgressSnapshot();
	return outcome;
}

void CTransferExecutor::accountRenamedSubtree(const SourceNode& node)
{
	_context.addCompletedItems(node.subtreeItems);
	_context.progress().advanceWithoutTransfer(node.subtreeBytes, node.subtreeItems);
	_context.publishProgressSnapshot();
}

// --- Staged transfer (copy, and the copy-based move fallback) ---

std::optional<NodeOutcome> CTransferExecutor::stagedFileTransferWithPolicy(const EntrySnapshot& source, const CEntryPath& destination,
	const ReplacementMode replacement, const PublishedSourceAction sourceAction, const bool makeWritableAuthorized)
{
	for (;;)
	{
		if (!_context.checkpoint())
			return NodeOutcome::Cancelled;

		_context.progress().setCurrentEntry(source.path, source.size);
		_context.publishProgressSnapshot();

		FailureDetails failure{};
		bool sessionCompleted = false;
		bool freshCollisionAtPublication = false;

		auto session = CStagedFileCopy::begin(source.path, destination);
		if (!session)
			failure = mv(session.error());
		else
		{
			bool transferFailed = false;
			for (;;)
			{
				if (!_context.checkpoint())
				{
					abortStagedSession(*session, source);
					_context.progress().currentEntryAbandoned();
					return NodeOutcome::Cancelled;
				}

				auto chunk = session->writeNext(_transferChunkSize);
				if (!chunk)
				{
					failure = mv(chunk.error());
					transferFailed = true;
					break;
				}
				if (chunk->bytesWritten != 0)
				{
					_context.progress().fileTransferAdvanced(chunk->bytesWritten);
					_context.addTransferredBytes(chunk->bytesWritten);
					_context.publishProgressSnapshot();
				}
				if (chunk->readyToCommit)
					break;
			}

			if (!transferFailed)
			{
				// The durability contract: flushing is required exactly where publication destroys the only
				// other copy - an authorized replacement, and every owned-source move (removal follows).
				const CommitDurability durability = (sourceAction == PublishedSourceAction::RemoveOwnedSource || replacement == ReplacementMode::ReplaceExistingFile)
					? CommitDurability::FlushBeforePublish : CommitDurability::NoFlush;
				if (auto committed = session->commit(replacement, durability); committed)
					sessionCompleted = true;
				else
					failure = mv(committed.error());
			}

			if (!sessionCompleted)
			{
				abortStagedSession(*session, source);
				// A publication-time AlreadyExists may re-enter resolution, but only when fresh inspection
				// proves the new collision really exists (nothing was published either way).
				if (failure.action == FailedAction::PublishDestination
					&& failure.filesystemError.category == FileErrorCategory::AlreadyExists)
				{
					if (const auto fresh = inspectEntry(destination); fresh && fresh->has_value())
						freshCollisionAtPublication = true;
				}
			}
		}

		if (sessionCompleted)
		{
			// Publication is the move's commit point: from here the committed cleanup segment runs to its end.
			if (sourceAction == PublishedSourceAction::RemoveOwnedSource)
				return removePublishedSourceWithPolicy(source, destination, makeWritableAuthorized);

			_context.addCompletedItems(1);
			_context.progress().itemCompleted();
			_context.progress().clearCurrentEntry();
			_context.publishProgressSnapshot();
			return NodeOutcome::Completed;
		}

		_context.progress().currentEntryAbandoned();
		if (freshCollisionAtPublication)
			return {};

		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, source, {}, mv(failure) });
		if (!decision || decision->action == DecisionAction::Cancel)
			return NodeOutcome::Cancelled;
		if (decision->action == DecisionAction::Skip)
		{
			_context.addSkippedItems(1);
			_context.progress().advanceWithoutTransfer(source.size, 1);
			_context.progress().clearCurrentEntry();
			_context.publishProgressSnapshot();
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry); // A new staging session, from the top
	}
}

void CTransferExecutor::abortStagedSession(CStagedFileCopy& session, const EntrySnapshot& source)
{
	if (auto aborted = session.abort(); !aborted)
		_context.recordWarning(OperationDiagnostic{ mv(aborted.error()), source, {} });
}

void CTransferExecutor::recordTimestampWarning(const EntrySnapshot& source, const CEntryPath& destination, CFileSystemError error)
{
	_context.recordWarning(OperationDiagnostic{ FailureDetails{ FailedAction::PreserveDirectoryTimestamps, mv(error) },
		source, EntrySnapshot{ destination, OperationEntryKind::Directory, 0 } });
}

void CTransferExecutor::accountSkippedSubtree(const SourceNode& node)
{
	_context.addSkippedItems(node.subtreeItems);
	_context.progress().advanceWithoutTransfer(node.subtreeBytes, node.subtreeItems);
	_context.publishProgressSnapshot();
}

void CTransferExecutor::accountAlreadySatisfiedSubtree(const SourceNode& node)
{
	_context.addAlreadySatisfiedItems(node.subtreeItems);
	_context.progress().advanceWithoutTransfer(node.subtreeBytes, node.subtreeItems);
	_context.publishProgressSnapshot();
}

void CTransferExecutor::rootTotalsResolved(const uint64_t bytes, const size_t items)
{
	assert_debug_only(_rootsWithUnresolvedTotals > 0);
	_knownTotalBytes += bytes;
	_knownTotalItems += items;
	if (--_rootsWithUnresolvedTotals == 0)
		_context.progress().setTotals(_knownTotalBytes, _knownTotalItems);
}

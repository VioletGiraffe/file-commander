#include "ctransferexecutor.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"
#include "cstagedfilecopy.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

CTransferExecutor::CTransferExecutor(COperationExecutionContext& context, const uint64_t transferChunkSize) noexcept
	: _context{ context }
	, _transferChunkSize{ transferChunkSize }
{
	assert_debug_only(transferChunkSize > 0);
}

OperationSummary CTransferExecutor::run(const TransferRequest& request)
{
	assert_debug_only(request.kind == TransferKind::Copy); // Move routing arrives with WP7

	const auto intents = rootTransferIntents(request);
	_rootsWithUnresolvedTotals = intents.size();

	bool anyCancelled = false;
	bool anyFailed = false;
	for (const RootTransferIntent& intent : intents)
	{
		const NodeOutcome outcome = copyRoot(intent);
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
		{
			_context.addSkippedItems(1);
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry);
	}
}

NodeOutcome CTransferExecutor::copyRoot(const RootTransferIntent& intent)
{
	if (!_context.checkpoint())
	{
		rootTotalsResolved(0, 0);
		return NodeOutcome::Cancelled;
	}

	auto rootEntry = inspectSourceRoot(intent.source);
	if (const auto* endedOutcome = std::get_if<NodeOutcome>(&rootEntry))
	{
		rootTotalsResolved(0, 0);
		return *endedOutcome;
	}
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
	{
		_context.addSkippedItems(1); // Unscanned: only the root entry itself is known
		rootTotalsResolved(0, 0);
		return NodeOutcome::Skipped;
	}
	if (std::holds_alternative<CancelOperation>(choice))
	{
		rootTotalsResolved(0, 0);
		return NodeOutcome::Cancelled;
	}
	if (std::holds_alternative<AlreadySatisfied>(choice))
	{
		_context.addAlreadySatisfiedItems(1);
		rootTotalsResolved(0, 0);
		return NodeOutcome::AlreadySatisfied;
	}

	// Build the manifest with the local retry policy; the diagnostic names the entry that failed to scan.
	std::optional<SourceNode> tree;
	while (!tree)
	{
		auto built = buildSourceTree(_context, root, SourceTreeBuildMode::MaterializingTransfer);
		if (auto* node = std::get_if<SourceNode>(&built))
		{
			tree = mv(*node);
			break;
		}
		if (std::holds_alternative<ScanCancelled>(built))
		{
			rootTotalsResolved(0, 0);
			return NodeOutcome::Cancelled;
		}

		auto& diagnostic = std::get<OperationDiagnostic>(built);
		const auto decision = _context.resolveDecision(OperationIssue{ IssueKind::ActionFailed, mv(diagnostic.source), {}, mv(diagnostic.failure) });
		if (!decision || decision->action == DecisionAction::Cancel)
		{
			rootTotalsResolved(0, 0);
			return NodeOutcome::Cancelled;
		}
		if (decision->action == DecisionAction::Skip)
		{
			_context.addSkippedItems(1);
			rootTotalsResolved(0, 0);
			return NodeOutcome::Skipped;
		}
		assert_debug_only(decision->action == DecisionAction::Retry);
	}

	rootTotalsResolved(tree->subtreeBytes, tree->subtreeItems);
	_context.publishProgressSnapshot(); // Back to Working, with totals possibly exact from here on
	return runDirectoryNode(*tree, TransferNodePosition::SelectedRoot, mv(choice));
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
		return runDirectoryNode(node, position, resolveDirectoryDestination(_context, node.entry, mv(proposedDestination), position));

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
		const auto outcome = copyFileWithPolicy(node.entry, use.path, use.replacement);
		if (outcome)
			return *outcome;
		proposedDestination = mv(use.path); // A new collision appeared at publication: resolve it freshly
	}
}

NodeOutcome CTransferExecutor::runDirectoryNode(const SourceNode& node, const TransferNodePosition position, DestinationChoice choice)
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
			return copyDirectoryContents(node, merge->path, false);

		const auto& use = std::get<UseDestination>(choice);
		const auto created = CFileSystemMutator::createDirectories(use.path);
		if (created)
		{
			if (*created == DirectoryCreationOutcome::CreatedFinalDirectory)
				return copyDirectoryContents(node, use.path, true);

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
	_context.addCompletedItem(); // The directory entry itself: created or merged successfully
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

std::optional<NodeOutcome> CTransferExecutor::copyFileWithPolicy(const EntrySnapshot& source, const CEntryPath& destination, const ReplacementMode replacement)
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
				// The durability contract: an authorized replacement destroys the previous version, so its
				// data must reach storage before publication; a fresh copy's source still exists.
				const CommitDurability durability = replacement == ReplacementMode::ReplaceExistingFile
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
			_context.addCompletedItem();
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

#pragma once

#include "cdestinationresolver.h"
#include "csourcetreebuilder.h"

class COperationExecutionContext;
class CStagedFileCopy;

// The synchronous recursive transfer executor. WP5 scope: copy execution; move routing (rename-first,
// committed source cleanup) arrives with WP7. Runs entirely on the caller's thread; all environment
// interaction - checkpoints, decisions, progress, diagnostics - goes through the execution context.
class CTransferExecutor
{
public:
	explicit CTransferExecutor(COperationExecutionContext& context, uint64_t transferChunkSize = 8 * 1024 * 1024) noexcept;

	[[nodiscard]] OperationSummary run(const TransferRequest& request);

private:
	[[nodiscard]] NodeOutcome copyRoot(const RootTransferIntent& intent);

	// Fresh root inspection with the local ActionFailed retry policy; the outcome alternative ends the root.
	[[nodiscard]] std::variant<EntrySnapshot, NodeOutcome> inspectSourceRoot(const CEntryPath& source);

	// Dispatches one manifest node: Other handling, resolution, file copy or directory recursion.
	[[nodiscard]] NodeOutcome copyNode(const SourceNode& node, CEntryPath proposedDestination, TransferNodePosition position);

	// Acts on an already-resolved directory disposition, re-entering resolution when the destination
	// races; owns the create-versus-merge disposition.
	[[nodiscard]] NodeOutcome runDirectoryNode(const SourceNode& node, TransferNodePosition position, DestinationChoice choice);

	// Creates/merges done: copies the children and finalizes timestamps for an operation-created directory.
	[[nodiscard]] NodeOutcome copyDirectoryContents(const SourceNode& node, const CEntryPath& destination, bool operationCreated);

	// One complete staged-copy session per attempt with the local Retry/Skip/Cancel policy.
	// nullopt = a new destination collision appeared at publication; the caller re-enters resolution.
	[[nodiscard]] std::optional<NodeOutcome> copyFileWithPolicy(const EntrySnapshot& source, const CEntryPath& destination, ReplacementMode replacement);

	void abortStagedSession(CStagedFileCopy& session, const EntrySnapshot& source);
	void recordTimestampWarning(const EntrySnapshot& source, const CEntryPath& destination, CFileSystemError error);

	void accountSkippedSubtree(const SourceNode& node);
	void accountAlreadySatisfiedSubtree(const SourceNode& node);
	// A root's contribution to the aggregate totals; publishes exact totals once every root's is known.
	void rootTotalsResolved(uint64_t bytes, size_t items);

	COperationExecutionContext& _context;
	const uint64_t _transferChunkSize;

	size_t _rootsWithUnresolvedTotals = 0;
	uint64_t _knownTotalBytes = 0;
	size_t _knownTotalItems = 0;
};

#pragma once

#include "cdestinationresolver.h"
#include "csourcetreebuilder.h"

class COperationExecutionContext;
class CStagedFileCopy;

// The synchronous recursive transfer executor for copy and move. Runs entirely on the caller's thread; all
// environment interaction - checkpoints, decisions, progress, diagnostics - goes through the execution context.
// Move is rename-first: native rename is attempted at every useful owned root/subtree boundary, and only a
// classified CrossDevice result selects the staged-copy fallback with its committed source-cleanup segment.
class CTransferExecutor
{
public:
	explicit CTransferExecutor(COperationExecutionContext& context, uint64_t transferChunkSize = 8 * 1024 * 1024) noexcept;

	[[nodiscard]] OperationSummary run(const TransferRequest& request);

private:
	// What happens to the source entry after successful staged publication.
	enum class PublishedSourceAction
	{
		RetainSource,     // Copy, and move materialization of borrowed (reached-through-link) content
		RemoveOwnedSource // Owned move: the committed cleanup segment follows publication
	};

	// Why a rename attempt could not complete; each selects a different continuation.
	enum class RenameBlock
	{
		DestinationOccupied, // Destination resolution decides
		CrossDevice          // The staged-copy fallback takes over for this subtree
	};

	// --- Shared by copy and move ---

	// Fresh root inspection with the local ActionFailed retry policy; the outcome alternative ends the root.
	[[nodiscard]] std::variant<EntrySnapshot, NodeOutcome> inspectSourceRoot(const CEntryPath& source);

	// The materializing manifest with the local ActionFailed retry policy.
	[[nodiscard]] std::variant<SourceNode, NodeOutcome> buildManifestWithRetry(const EntrySnapshot& root);

	// Totals and summary accounting for a root that ended before its manifest was built; the root entry
	// itself is the only known item.
	[[nodiscard]] NodeOutcome finishUnscannedRoot(NodeOutcome outcome);

	// Acts on an already-resolved directory disposition, re-entering resolution when the destination
	// races; owns the create-versus-merge disposition. Dispatches contents by the request kind.
	[[nodiscard]] NodeOutcome runDirectoryNode(const SourceNode& node, TransferNodePosition position, DestinationChoice choice, bool knownCrossDevice);

	// One complete staged-copy session per attempt with the local Retry/Skip/Cancel policy; for
	// RemoveOwnedSource, successful publication continues into the committed cleanup segment.
	// nullopt = a new destination collision appeared at publication; the caller re-enters resolution.
	[[nodiscard]] std::optional<NodeOutcome> stagedFileTransferWithPolicy(const EntrySnapshot& source, const CEntryPath& destination,
		ReplacementMode replacement, PublishedSourceAction sourceAction, bool makeWritableAuthorized);

	void abortStagedSession(CStagedFileCopy& session, const EntrySnapshot& source);
	void recordTimestampWarning(const EntrySnapshot& source, const CEntryPath& destination, CFileSystemError error);

	void accountSkippedSubtree(const SourceNode& node);
	void accountAlreadySatisfiedSubtree(const SourceNode& node);
	// A root's contribution to the aggregate totals; publishes exact totals once every root's is known.
	void rootTotalsResolved(uint64_t bytes, size_t items);

	// --- Copy ---

	[[nodiscard]] NodeOutcome copyRoot(const RootTransferIntent& intent);

	// Dispatches one manifest node: Other handling, resolution, file copy or directory recursion.
	[[nodiscard]] NodeOutcome copyNode(const SourceNode& node, CEntryPath proposedDestination, TransferNodePosition position);

	// Creates/merges done: copies the children and finalizes timestamps for an operation-created directory.
	[[nodiscard]] NodeOutcome copyDirectoryContents(const SourceNode& node, const CEntryPath& destination, bool operationCreated);

	// --- Move ---

	[[nodiscard]] NodeOutcome moveRoot(const RootTransferIntent& intent);

	// Dispatches one manifest node: rename-first for owned entries, then Other handling, resolution,
	// staged transfer or directory recursion. knownCrossDevice suppresses rename attempts for the
	// whole subtree once one boundary has classified as cross-device.
	[[nodiscard]] NodeOutcome moveNode(const SourceNode& node, CEntryPath proposedDestination, TransferNodePosition position, bool knownCrossDevice);

	// The file-node resolution loop: post-resolution rename (atomic same-filesystem replacement), the
	// pre-publication read-only policy, and the staged transfer with publication-race re-resolution.
	[[nodiscard]] NodeOutcome moveFileNode(const SourceNode& node, CEntryPath proposedDestination, bool knownCrossDevice);

	// Moves the children and finalizes timestamps like the copy counterpart, then removes the emptied
	// owned source directory (or link entry) through the committed cleanup policy. Cleanup requires every
	// child strictly Completed: an AlreadySatisfied child keeps its source entry in place.
	[[nodiscard]] NodeOutcome moveDirectoryContents(const SourceNode& node, const CEntryPath& destination, bool operationCreated, bool knownCrossDevice);

	// The local rename retry loop; transient failures prompt ActionFailed(RenameEntry) with Retry/Skip/Cancel.
	[[nodiscard]] std::variant<NodeOutcome, RenameBlock> renameEntryWithPolicy(const EntrySnapshot& source, const CEntryPath& destination, ReplacementMode replacement);

	// Pre-commit ReadOnlySourceRemoval for an owned non-link regular file about to take the copy-based
	// path: bool = MakeWritable authorization (held locally, applied only after publication).
	[[nodiscard]] std::variant<bool, NodeOutcome> resolveMoveReadOnlyPreflight(const EntrySnapshot& entry);

	// The committed cleanup segment: publication succeeded, so the only end states are "source removed"
	// (Completed) or one recorded terminal failure with both entries retained (Failed, or Cancelled when
	// cancellation overrides a prompt). No cancellation checkpoint; every prompt is item-only.
	[[nodiscard]] NodeOutcome removePublishedSourceWithPolicy(const EntrySnapshot& entry, const CEntryPath& destination, bool makeWritableAuthorized);

	[[nodiscard]] NodeOutcome recordTerminalCleanupFailure(const EntrySnapshot& entry, const CEntryPath& destination,
		FailureDetails failure, NodeOutcome outcome);

	// A subtree relocated by one native rename: every manifest item completed, no bytes streamed.
	void accountRenamedSubtree(const SourceNode& node);

	COperationExecutionContext& _context;
	const uint64_t _transferChunkSize;

	TransferKind _requestKind = TransferKind::Copy;
	size_t _rootsWithUnresolvedTotals = 0;
	uint64_t _knownTotalBytes = 0;
	size_t _knownTotalItems = 0;
};

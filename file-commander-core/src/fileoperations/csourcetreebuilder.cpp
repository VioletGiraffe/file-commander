#include "csourcetreebuilder.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"
#include "thiniobridge.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()
#include "utility/on_scope_exit.hpp"

#include <algorithm>

namespace
{

struct SourceTreeBuildLimits
{
	size_t maximumDepth;
	size_t maximumNodeCount;
#ifdef FILE_OPERATIONS_TEST_HOOKS
	DirectoryLinkIdentityModeForTesting directoryLinkIdentityMode = DirectoryLinkIdentityModeForTesting::Native;
#endif
};

// Root is depth 1. These ceilings bound every recursive manifest consumer because executors only traverse
// trees successfully produced here.
constexpr SourceTreeBuildLimits productionLimits{ .maximumDepth = 300, .maximumNodeCount = 2'000'000 };
constexpr size_t directoryChildrenPerCheckpoint = 64;

struct BuildState
{
	COperationExecutionContext& context;
	const SourceTreeBuildMode mode;
	const SourceTreeBuildLimits limits;
	size_t discoveredCount = 0;
	std::vector<thin_io::entry_identity> activeBranchIdentities; // Directories on the current recursion path (materializing mode only)

	// Why buildNode() returned BuildStopped; exactly one is set on termination.
	std::optional<OperationDiagnostic> failure;
	bool cancelled = false;
};

enum class NodePosition
{
	Root,
	Child
};

struct ChildVanished {};
struct BuildStopped {};
using BuildNodeResult = std::variant<SourceNode, ChildVanished, BuildStopped>;

void publishScanProgress(BuildState& state, const CEntryPath& currentEntry)
{
	ProgressSnapshot snapshot;
	snapshot.phase = OperationPhase::Scanning;
	snapshot.currentEntry = currentEntry;
	snapshot.itemsProcessed = state.discoveredCount;
	state.context.publishProgress(snapshot); // Totals stay absent: a partial aggregate must not look exact
}

void failBuild(BuildState& state, EntrySnapshot failedEntry, CFileSystemError error)
{
	state.failure = OperationDiagnostic{ FailureDetails{ FailedAction::InspectSource, mv(error) }, mv(failedEntry), {} };
}

bool buildCancelledAtCheckpoint(BuildState& state)
{
	if (state.context.checkpoint())
		return false;
	state.cancelled = true;
	return true;
}

CFileSystemError unrepresentableSourceNameError()
{
	return { FileErrorCategory::Unsupported, 0,
		QStringLiteral("A source entry name cannot be represented without changing its native spelling") };
}

CFileSystemError sourceTreeDepthLimitError()
{
	return { FileErrorCategory::Unsupported, 0, QStringLiteral("The source tree exceeds the supported traversal depth") };
}

CFileSystemError sourceTreeNodeLimitError()
{
	return { FileErrorCategory::Unsupported, 0, QStringLiteral("The source tree contains too many entries to process safely") };
}

// Classifies one listed child into a snapshot. nullopt without a recorded failure means the entry
// vanished between the listing and its inspection - a race, the child is simply not part of the tree.
std::optional<EntrySnapshot> classifyChild(BuildState& state, CEntryPath childPath, const thin_io::directory_entry& listed)
{
	if (isLinkEntry(listed.attributes))
	{
		if (state.mode == SourceTreeBuildMode::PermanentDelete)
		{
			// Delete addresses the link entry itself; its own directory-ness selects the removal primitive.
			const bool directoryEntry = listed.attributes.kind == thin_io::entry_kind::directory;
			return EntrySnapshot{ mv(childPath), directoryEntry ? OperationEntryKind::DirectoryLink : OperationEntryKind::FileLink, 0 };
		}

		// Materialization needs the followed target's classification and size.
		auto inspected = inspectEntry(childPath);
		if (!inspected)
		{
			// The diagnostic keeps the link's own kind: a directory link must not be reported as a file link.
			const bool directoryEntry = listed.attributes.kind == thin_io::entry_kind::directory;
			failBuild(state, EntrySnapshot{ mv(childPath), directoryEntry ? OperationEntryKind::DirectoryLink : OperationEntryKind::FileLink, 0 },
				mv(inspected.error()));
			return {};
		}
		if (!inspected->has_value())
			return {};
		return mv(**inspected);
	}

	switch (listed.attributes.kind)
	{
	case thin_io::entry_kind::directory:
		return EntrySnapshot{ mv(childPath), OperationEntryKind::Directory, 0 };
	case thin_io::entry_kind::regular_file:
	{
		uint64_t size = 0;
		if (listed.logical_size)
			size = *listed.logical_size;
		else
		{
			// POSIX listings carry no sizes
			const auto native = thinIoPath(childPath);
			const auto metadata = thin_io::get_entry_metadata(nativeCStr(native), thin_io::link_behavior::do_not_follow);
			if (!metadata)
			{
				const auto code = metadata.error().native_code;
				if (classifyNativeError(code) == FileErrorCategory::NotFound)
					return {};
				failBuild(state, EntrySnapshot{ mv(childPath), OperationEntryKind::RegularFile, 0 }, makeFileSystemError(code));
				return {};
			}
			size = metadata->logical_size;
		}
		return EntrySnapshot{ mv(childPath), OperationEntryKind::RegularFile, size };
	}
	default:
		return EntrySnapshot{ mv(childPath), OperationEntryKind::Other, 0 };
	}
}

// Recursive DFS producing one immutable node. A vanished child is recoverable by its parent; a stopped
// build has recorded either a failure or cancellation in state.
BuildNodeResult buildNode(BuildState& state, EntrySnapshot entry, const SourceOwnership ownership, const NodePosition position, const size_t depth)
{
	if (depth > state.limits.maximumDepth)
	{
		failBuild(state, mv(entry), sourceTreeDepthLimitError());
		return BuildStopped{};
	}
	if (state.discoveredCount >= state.limits.maximumNodeCount)
	{
		failBuild(state, mv(entry), sourceTreeNodeLimitError());
		return BuildStopped{};
	}

	const size_t activeIdentityCountOnEntry = state.activeBranchIdentities.size();
	// A frame owns every branch identity added beneath it. Restore the incoming depth on every exit,
	// including cancellation, failure, and a recoverable child disappearance.
	EXEC_ON_SCOPE_EXIT([&state, activeIdentityCountOnEntry] {
		assert_debug_only(state.activeBranchIdentities.size() >= activeIdentityCountOnEntry);
		while (state.activeBranchIdentities.size() > activeIdentityCountOnEntry)
			state.activeBranchIdentities.pop_back();
	});

	++state.discoveredCount;
	publishScanProgress(state, entry.path);

	SourceNode node{ .entry = mv(entry), .ownership = ownership };

	bool descend = false;
	if (node.entry.kind == OperationEntryKind::Directory)
	{
		descend = true;
		if (state.mode == SourceTreeBuildMode::MaterializingTransfer)
		{
			// The active-branch identity is what makes this directory recognizable as a deeper link's cycle target.
			const auto identity = readEntryIdentity(node.entry.path, thin_io::link_behavior::follow);
			if (!identity)
			{
				// A vanished directory is reported by the listing below; anything else aborts the build.
				if (identity.error().category != FileErrorCategory::NotFound)
				{
					failBuild(state, node.entry, identity.error());
					return BuildStopped{};
				}
			}
			else if (*identity)
				state.activeBranchIdentities.push_back(**identity);
			// Without an identity this directory cannot participate in early cycle detection; the fixed
			// traversal limits remain the fallback.
		}
	}
	else if (node.entry.kind == OperationEntryKind::DirectoryLink && state.mode == SourceTreeBuildMode::MaterializingTransfer)
	{
		auto targetIdentity = readEntryIdentity(node.entry.path, thin_io::link_behavior::follow);
#ifdef FILE_OPERATIONS_TEST_HOOKS
		if (targetIdentity && state.limits.directoryLinkIdentityMode == DirectoryLinkIdentityModeForTesting::Unavailable)
			targetIdentity = std::optional<thin_io::entry_identity>{};
#endif
		if (!targetIdentity)
		{
			// A broken link stays a leaf entry. Any other failure aborts the build: silently materializing
			// nothing from an unreadable target would make the copy falsely look complete.
			if (targetIdentity.error().category != FileErrorCategory::NotFound)
			{
				failBuild(state, node.entry, targetIdentity.error());
				return BuildStopped{};
			}
		}
		else if (*targetIdentity)
		{
			if (std::find(state.activeBranchIdentities.begin(), state.activeBranchIdentities.end(), **targetIdentity) == state.activeBranchIdentities.end())
			{
				descend = true;
				state.activeBranchIdentities.push_back(**targetIdentity);
			}
			// A target already on the recursion path stays a leaf: traversing it again could only
			// duplicate content or recurse forever.
		}
		else
		{
			// Identity is an optimization for terminating cycles early, not a prerequisite for correct
			// materialization. The depth and node-count limits bound an identity-less cycle before execution.
			descend = true;
		}
	}

	if (descend)
	{
		if (buildCancelledAtCheckpoint(state))
			return BuildStopped{};

		const auto native = thinIoPath(node.entry.path);
		const auto listing = thin_io::list_directory(nativeCStr(native));
		if (!listing)
		{
			auto error = makeFileSystemError(listing.error().native_code);
			if (position == NodePosition::Child && error.category == FileErrorCategory::NotFound)
				return ChildVanished{};
			failBuild(state, node.entry, mv(error));
			return BuildStopped{};
		}
		if (buildCancelledAtCheckpoint(state))
			return BuildStopped{};

		const bool borrowedChildren = ownership == SourceOwnership::BorrowedThroughDirectoryLink || node.entry.kind == OperationEntryKind::DirectoryLink;
		const SourceOwnership childOwnership = borrowedChildren ? SourceOwnership::BorrowedThroughDirectoryLink : SourceOwnership::Owned;

		const size_t remainingNodeBudget = state.limits.maximumNodeCount - state.discoveredCount;
		node.children.reserve(std::min(listing->size(), remainingNodeBudget));
		size_t childrenSinceCheckpoint = 0;
		for (const auto& listed : *listing)
		{
			if (childrenSinceCheckpoint == directoryChildrenPerCheckpoint)
			{
				if (buildCancelledAtCheckpoint(state))
					return BuildStopped{};
				childrenSinceCheckpoint = 0;
			}
			++childrenSinceCheckpoint;

			auto childName = decodeNativeNameLosslessly(listed.name);
			if (!childName)
			{
				// The parent is the narrowest path that can be named faithfully. Never manufacture a child
				// path: it could alias a different entry after the lossy decode/encode round trip.
				failBuild(state, node.entry, unrepresentableSourceNameError());
				return BuildStopped{};
			}

			auto childEntry = classifyChild(state, node.entry.path.child(*childName), listed);
			if (!childEntry)
			{
				if (state.failure)
					return BuildStopped{};
				continue; // Vanished between listing and classification
			}

			auto childResult = buildNode(state, mv(*childEntry), childOwnership, NodePosition::Child, depth + 1);
			if (auto* childNode = std::get_if<SourceNode>(&childResult))
				node.children.push_back(mv(*childNode));
			else if (std::holds_alternative<BuildStopped>(childResult))
				return BuildStopped{};
		}
	}

	node.subtreeBytes = node.entry.size;
	node.subtreeItems = 1;
	for (const SourceNode& child : node.children)
	{
		node.subtreeBytes += child.subtreeBytes;
		node.subtreeItems += child.subtreeItems;
	}
	return node;
}

SourceTreeResult buildSourceTreeWithLimits(COperationExecutionContext& context, EntrySnapshot root, const SourceTreeBuildMode mode,
	const SourceTreeBuildLimits limits)
{
	BuildState state{ .context = context, .mode = mode, .limits = limits };
	auto result = buildNode(state, mv(root), SourceOwnership::Owned, NodePosition::Root, 1);
	assert_debug_only(state.activeBranchIdentities.empty());
	if (auto* tree = std::get_if<SourceNode>(&result))
		return mv(*tree);

	assert_debug_only(std::holds_alternative<BuildStopped>(result)); // A root is never a recoverable child disappearance
	if (state.failure)
		return mv(*state.failure);

	assert_debug_only(state.cancelled);
	return ScanCancelled{};
}

} // namespace

SourceTreeResult buildSourceTree(COperationExecutionContext& context, EntrySnapshot root, const SourceTreeBuildMode mode)
{
	return buildSourceTreeWithLimits(context, mv(root), mode, productionLimits);
}

#ifdef FILE_OPERATIONS_TEST_HOOKS
SourceTreeResult buildSourceTreeForTesting(COperationExecutionContext& context, EntrySnapshot root, const SourceTreeBuildMode mode,
	const size_t maximumDepth, const size_t maximumNodeCount, const DirectoryLinkIdentityModeForTesting directoryLinkIdentityMode)
{
	return buildSourceTreeWithLimits(context, mv(root), mode,
		SourceTreeBuildLimits{ .maximumDepth = maximumDepth, .maximumNodeCount = maximumNodeCount,
			.directoryLinkIdentityMode = directoryLinkIdentityMode });
}
#endif

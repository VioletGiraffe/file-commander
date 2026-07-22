#include "csourcetreebuilder.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"
#include "thiniobridge.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

#include <algorithm>

namespace
{

struct BuildState
{
	COperationExecutionContext& context;
	const SourceTreeBuildMode mode;
	size_t discoveredCount = 0;
	std::vector<thin_io::entry_identity> activeBranchIdentities; // Directories on the current recursion path (materializing mode only)

	// Why buildNode() returned without a tree; exactly one is set on termination.
	std::optional<OperationDiagnostic> failure;
	bool cancelled = false;
};

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
			failBuild(state, EntrySnapshot{ mv(childPath), OperationEntryKind::FileLink, 0 }, mv(inspected.error()));
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

// Recursive DFS producing one immutable node. nullopt = the build terminated; state says why.
std::optional<SourceNode> buildNode(BuildState& state, EntrySnapshot entry, const SourceOwnership ownership)
{
	++state.discoveredCount;
	publishScanProgress(state, entry.path);

	SourceNode node{ .entry = mv(entry), .ownership = ownership };

	bool descend = false;
	bool identityPushed = false;
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
					return {};
				}
			}
			else if (*identity)
			{
				state.activeBranchIdentities.push_back(**identity);
				identityPushed = true;
			}
			// An identity-less filesystem cannot be cycle-protected, but such filesystems do not support
			// links either, so an unprotected branch cannot loop.
		}
	}
	else if (node.entry.kind == OperationEntryKind::DirectoryLink && state.mode == SourceTreeBuildMode::MaterializingTransfer)
	{
		const auto targetIdentity = readEntryIdentity(node.entry.path, thin_io::link_behavior::follow);
		if (!targetIdentity)
		{
			// A broken link stays a leaf entry. Any other failure aborts the build: silently materializing
			// nothing from an unreadable target would make the copy falsely look complete.
			if (targetIdentity.error().category != FileErrorCategory::NotFound)
			{
				failBuild(state, node.entry, targetIdentity.error());
				return {};
			}
		}
		else if (*targetIdentity)
		{
			if (std::find(state.activeBranchIdentities.begin(), state.activeBranchIdentities.end(), **targetIdentity) == state.activeBranchIdentities.end())
			{
				descend = true;
				state.activeBranchIdentities.push_back(**targetIdentity);
				identityPushed = true;
			}
			// A target already on the recursion path stays a leaf: traversing it again could only
			// duplicate content or recurse forever.
		}
		// No identity available: the target cannot be proven distinct from every ancestor, so do not traverse.
	}

	if (descend)
	{
		if (!state.context.checkpoint())
		{
			state.cancelled = true;
			return {};
		}

		const auto native = thinIoPath(node.entry.path);
		const auto listing = thin_io::list_directory(nativeCStr(native));
		if (!listing)
		{
			failBuild(state, node.entry, makeFileSystemError(listing.error().native_code));
			return {};
		}

		const bool borrowedChildren = ownership == SourceOwnership::BorrowedThroughDirectoryLink || node.entry.kind == OperationEntryKind::DirectoryLink;
		const SourceOwnership childOwnership = borrowedChildren ? SourceOwnership::BorrowedThroughDirectoryLink : SourceOwnership::Owned;

		node.children.reserve(listing->size());
		for (const auto& listed : *listing)
		{
			auto childEntry = classifyChild(state, node.entry.path.child(fromNativeName(listed.name)), listed);
			if (!childEntry)
			{
				if (state.failure)
					return {};
				continue; // Vanished between listing and classification
			}

			auto childNode = buildNode(state, mv(*childEntry), childOwnership);
			if (!childNode)
				return {};
			node.children.push_back(mv(*childNode));
		}
	}

	if (identityPushed)
		state.activeBranchIdentities.pop_back();

	node.subtreeBytes = node.entry.size;
	node.subtreeItems = 1;
	for (const SourceNode& child : node.children)
	{
		node.subtreeBytes += child.subtreeBytes;
		node.subtreeItems += child.subtreeItems;
	}
	return node;
}

} // namespace

SourceTreeResult buildSourceTree(COperationExecutionContext& context, EntrySnapshot root, const SourceTreeBuildMode mode)
{
	BuildState state{ .context = context, .mode = mode };
	auto tree = buildNode(state, mv(root), SourceOwnership::Owned);
	if (tree)
		return mv(*tree);
	if (state.failure)
		return mv(*state.failure);

	assert_debug_only(state.cancelled);
	return ScanCancelled{};
}

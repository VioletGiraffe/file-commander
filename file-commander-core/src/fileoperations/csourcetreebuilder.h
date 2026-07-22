#pragma once

#include "fileoperationtypes.h"

#include <variant>

class COperationExecutionContext;

enum class SourceOwnership : uint8_t
{
	Owned,
	BorrowedThroughDirectoryLink // Reached via a directory link: materialized at the destination, never removed by move cleanup
};

// One node of the immutable source manifest. Planning and presentation data only, never permission to
// assume the filesystem is unchanged: every mutation revalidates what it needs immediately before acting.
struct SourceNode
{
	EntrySnapshot entry;
	std::vector<SourceNode> children;
	uint64_t subtreeBytes = 0; // entry.size plus all descendants
	size_t subtreeItems = 0;   // This node plus all descendants
	SourceOwnership ownership = SourceOwnership::Owned;
};

// The manifest is O(number of entries), so per-node size multiplies into every large tree. Accidental
// growth must fail the build; a deliberate layout change updates these consciously.
static_assert(sizeof(void*) != 8 || sizeof(EntrySnapshot) == 40);
static_assert(sizeof(void*) != 8 || sizeof(SourceNode) == 88);

// Tree construction is operation-specific.
enum class SourceTreeBuildMode
{
	MaterializingTransfer, // Copy / copy-based move: directory links are traversed (their content borrowed), file links carry followed target sizes
	PermanentDelete        // Links of both kinds are leaf entries; nothing is ever followed
};

struct ScanCancelled {};

// SourceNode: the complete manifest for the root; OperationDiagnostic: the entry that could not be
// scanned plus the structured error (the build never returns a partial tree); ScanCancelled: cancellation
// won at a checkpoint.
using SourceTreeResult = std::variant<SourceNode, OperationDiagnostic, ScanCancelled>;

// Builds the manifest for one root whose fresh snapshot the caller already holds. Publishes Scanning
// progress through the context - the current root's discovered count with all totals absent - and honors
// its cancellation checkpoints. Directory-link cycles terminate via filesystem identities held for the
// active recursion branch only; no identity is stored in the result.
[[nodiscard]] SourceTreeResult buildSourceTree(COperationExecutionContext& context, EntrySnapshot root, SourceTreeBuildMode mode);

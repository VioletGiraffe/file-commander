#pragma once

#include "fileoperationtypes.h"

#include <optional>

enum class InlineRenameStatus
{
	Renamed,             // The entry was renamed
	NothingToDo,         // Same spelling, or the destination is the source itself; no rename was needed
	ReplacementRequired, // A file-like destination exists; confirm, then call again with replaceExistingFile = true
	Rejected,            // The destination is occupied by an entry inline rename will not replace (see the kinds)
	RejectedInvalidName, // The new name is not a usable entry name
	Failed               // A native inspection or rename failed
};

struct InlineRenameResult
{
	InlineRenameStatus status;
	std::optional<OperationEntryKind> sourceKind;      // Rejected: for the type-aware message
	std::optional<OperationEntryKind> destinationKind; // Rejected: for the type-aware message
	std::optional<FailureDetails> failure;             // Failed: the attempted action and native error
};

// The synchronous single-entry rename command, applying the inline-rename matrix - deliberately distinct
// from the batch collision policy. One source and one new name in the same parent; no scanning, job,
// progress, worker, or decision wait. Reuses the shared name validation, entry inspection, same-object
// detection, and the native rename primitive.
//
// The matrix: same spelling or same object -> NothingToDo; a case-only change or an absent destination ->
// rename; a distinct file-like destination -> ReplacementRequired (the caller confirms, then calls again
// with replaceExistingFile = true to perform the atomic replacement); a real directory destination, or any
// occupied destination for a directory-like source -> Rejected (inline rename never merges or replaces a
// folder); an invalid name -> RejectedInvalidName; a native failure -> Failed.
[[nodiscard]] InlineRenameResult inlineRename(const CEntryPath& source, const QString& newName, bool replaceExistingFile);

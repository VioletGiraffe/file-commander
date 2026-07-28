#include "inlinerename.h"
#include "centrypath.h"
#include "cfilesystemmutator.h"

#include "assert/advanced_assert.h"

#include <utility>

namespace
{

InlineRenameResult performRename(const CEntryPath& source, const CEntryPath& destination, const ReplacementMode mode)
{
	const auto result = CFileSystemMutator::renameEntry(source, destination, mode);
	if (result)
		return { .status = InlineRenameStatus::Renamed };

	return { .status = InlineRenameStatus::Failed, .failure = FailureDetails{ FailedAction::RenameEntry, result.error() } };
}

InlineRenameResult performCaseRespellOrRecognizeAlias(const CEntryPath& source, const CEntryPath& destination)
{
	auto result = CFileSystemMutator::renameEntry(source, destination, ReplacementMode::RequireAbsent);
	if (result)
		return { .status = InlineRenameStatus::Renamed };

	CFileSystemError error = std::move(result.error());
	if (error.category == FileErrorCategory::AlreadyExists)
	{
		// The temporary probe restored the source because the destination remained occupied. A surviving
		// hard-link alias is already satisfied; a raced-in different entry remains a real rename failure.
		const auto stillSameEntry = checkSameEntry(source, destination, thin_io::link_behavior::do_not_follow);
		if (!stillSameEntry)
			return { .status = InlineRenameStatus::Failed,
				.failure = FailureDetails{ FailedAction::InspectDestination, stillSameEntry.error() } };
		if (*stillSameEntry == SameEntryVerdict::Same)
			return { .status = InlineRenameStatus::NothingToDo };
	}

	return { .status = InlineRenameStatus::Failed, .failure = FailureDetails{ FailedAction::RenameEntry, std::move(error) } };
}

} // namespace

InlineRenameResult inlineRename(const CEntryPath& source, const QString& newName, const bool replaceExistingFile)
{
	using enum InlineRenameStatus;
	assert_debug_only(!source.isRoot());

	if (const NameRejection rejection = checkNewEntryName(newName); rejection != NameRejection::None)
		return { .status = RejectedInvalidName, .nameRejection = rejection };

	const CEntryPath destination = source.parent().child(newName);

	// Exact same spelling (case-sensitive comparison): nothing to do. A case-only change differs here and
	// proceeds to the rename, which is what preserves case-only renames on case-insensitive filesystems.
	if (source.value() == destination.value())
		return { .status = NothingToDo };

	const auto destinationInspect = inspectEntry(destination);
	if (!destinationInspect)
		return { .status = Failed, .failure = FailureDetails{ FailedAction::InspectDestination, destinationInspect.error() } };

	// Absent destination: a straight exclusive rename. This is also the case-only path on a case-sensitive
	// filesystem, where the new-case spelling genuinely names no existing entry.
	if (!destinationInspect->has_value())
		return performRename(source, destination, ReplacementMode::RequireAbsent);

	// The destination exists. If it is the source itself, this is either a case-only respell (the same file
	// under a different case, on a case-insensitive filesystem) or renaming onto one of the file's own
	// hardlink aliases. A case-only respell is performed through the primitive's exclusive temporary path;
	// aliasing the same file under a genuinely different name is already satisfied.
	const auto sameEntry = checkSameEntry(source, destination, thin_io::link_behavior::do_not_follow);
	if (!sameEntry)
		return { .status = Failed, .failure = FailureDetails{ FailedAction::InspectSource, sameEntry.error() } };
	if (*sameEntry == SameEntryVerdict::Same)
	{
		if (newName.compare(source.name(), Qt::CaseInsensitive) == 0)
			return performCaseRespellOrRecognizeAlias(source, destination);
		return { .status = NothingToDo };
	}

	// A distinct existing destination: the source's kind decides between replacement and rejection.
	const auto sourceInspect = inspectEntry(source);
	if (!sourceInspect)
		return { .status = Failed, .failure = FailureDetails{ FailedAction::InspectSource, sourceInspect.error() } };
	if (!sourceInspect->has_value())
		return { .status = Failed, .failure = FailureDetails{ FailedAction::InspectSource, CFileSystemError{ FileErrorCategory::NotFound, 0, {} } } };

	using enum OperationEntryKind;
	const OperationEntryKind sourceKind = (*sourceInspect)->kind;
	const OperationEntryKind destinationKind = (*destinationInspect)->kind;

	// Inline rename keeps one cross-platform matrix and rejects every directory-like entry. Batch transfer
	// additionally follows native capability: POSIX can atomically replace a directory symlink entry, while
	// Windows cannot replace a directory symlink or junction through either direct or staged publication.
	const auto isDirectoryLike = [](const OperationEntryKind kind) { return kind == Directory || kind == DirectoryLink; };
	if (isDirectoryLike(sourceKind) || isDirectoryLike(destinationKind))
		return { .status = Rejected, .sourceKind = sourceKind, .destinationKind = destinationKind };

	// File-like -> file-like (a regular file, a file link, or an other-entry): an atomic replacement, but
	// only after the caller confirms it.
	if (!replaceExistingFile)
		return { .status = ReplacementRequired, .sourceKind = sourceKind, .destinationKind = destinationKind };

	return performRename(source, destination, ReplacementMode::ReplaceExistingFile);
}

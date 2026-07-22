#include "cdestinationresolver.h"
#include "cfilesystemmutator.h"
#include "coperationexecutioncontext.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

namespace
{

// One prompt for a collision-type issue, owning the Rename input handling: an invalid or missing new
// name re-asks rather than failing the node. Returns the respelled proposal for a usable Rename,
// or the chosen non-rename action (cancellation collapses to Cancel).
std::variant<CEntryPath, DecisionAction> promptForCollision(COperationExecutionContext& context, const OperationIssue& issue, const CEntryPath& proposed)
{
	for (;;)
	{
		const auto decision = context.resolveDecision(issue);
		if (!decision)
			return DecisionAction::Cancel;
		if (decision->action != DecisionAction::Rename)
			return decision->action;

		const QString newName = decision->newName ? decision->newName->trimmed() : QString{};
		if (isValidEntryName(newName))
			return proposed.parent().child(newName);
	}
}

// Retry/Skip/Cancel for a resolution-time inspection failure; nullopt = Retry.
std::optional<DestinationChoice> resolveInspectionFailure(COperationExecutionContext& context, const EntrySnapshot& source, CFileSystemError error)
{
	const auto decision = context.resolveDecision(
		OperationIssue{ IssueKind::ActionFailed, source, {}, FailureDetails{ FailedAction::InspectDestination, mv(error) } });
	if (!decision || decision->action == DecisionAction::Cancel)
		return DestinationChoice{ CancelOperation{} };
	if (decision->action == DecisionAction::Skip)
		return DestinationChoice{ SkipNode{} };

	assert_debug_only(decision->action == DecisionAction::Retry);
	return {};
}

} // namespace

DestinationChoice resolveFileDestination(COperationExecutionContext& context, const EntrySnapshot& source, CEntryPath proposed)
{
	assert_debug_only(source.kind == OperationEntryKind::RegularFile || source.kind == OperationEntryKind::FileLink);

	for (;;)
	{
		const auto destination = inspectEntry(proposed);
		if (!destination) [[unlikely]]
		{
			if (auto verdict = resolveInspectionFailure(context, source, destination.error()))
				return mv(*verdict);
			continue;
		}

		if (!destination->has_value())
			return UseDestination{ mv(proposed), ReplacementMode::RequireAbsent };

		const auto sameEntry = checkSameEntry(source.path, proposed, thin_io::link_behavior::follow);
		if (!sameEntry) [[unlikely]]
		{
			if (auto verdict = resolveInspectionFailure(context, source, sameEntry.error()))
				return mv(*verdict);
			continue;
		}
		if (*sameEntry == SameEntryVerdict::Same)
			return AlreadySatisfied{};

		const EntrySnapshot& destinationEntry = **destination;
		const bool destinationIsRealDirectory = destinationEntry.kind == OperationEntryKind::Directory;
		const OperationIssue issue{ destinationIsRealDirectory ? IssueKind::TypeMismatch : IssueKind::FileReplacement,
			source, destinationEntry, {} };

		auto outcome = promptForCollision(context, issue, proposed);
		if (auto* respelled = std::get_if<CEntryPath>(&outcome))
		{
			proposed = mv(*respelled);
			continue;
		}

		switch (std::get<DecisionAction>(outcome))
		{
		case DecisionAction::Replace:
			assert_debug_only(!destinationIsRealDirectory);
			return UseDestination{ mv(proposed), ReplacementMode::ReplaceExistingFile };
		case DecisionAction::Skip:
			return SkipNode{};
		default:
			assert_debug_only(std::get<DecisionAction>(outcome) == DecisionAction::Cancel);
			return CancelOperation{};
		}
	}
}

DestinationChoice resolveDirectoryDestination(COperationExecutionContext& context, const EntrySnapshot& source,
	CEntryPath proposed, const TransferNodePosition position)
{
	assert_debug_only(source.kind == OperationEntryKind::Directory || source.kind == OperationEntryKind::DirectoryLink);

	for (;;)
	{
		const auto destination = inspectEntry(proposed);
		if (!destination) [[unlikely]]
		{
			if (auto verdict = resolveInspectionFailure(context, source, destination.error()))
				return mv(*verdict);
			continue;
		}

		if (!destination->has_value())
			return UseDestination{ mv(proposed), ReplacementMode::RequireAbsent };

		const auto sameEntry = checkSameEntry(source.path, proposed, thin_io::link_behavior::follow);
		if (!sameEntry) [[unlikely]]
		{
			if (auto verdict = resolveInspectionFailure(context, source, sameEntry.error()))
				return mv(*verdict);
			continue;
		}
		if (*sameEntry == SameEntryVerdict::Same)
			return AlreadySatisfied{};

		const EntrySnapshot& destinationEntry = **destination;
		if (destinationEntry.kind == OperationEntryKind::Directory)
		{
			// Structurally part of an already accepted merge: descendants collide silently.
			if (position == TransferNodePosition::Descendant)
				return MergeDirectory{ mv(proposed) };

			auto outcome = promptForCollision(context, OperationIssue{ IssueKind::RootDirectoryMerge, source, destinationEntry, {} }, proposed);
			if (auto* respelled = std::get_if<CEntryPath>(&outcome))
			{
				proposed = mv(*respelled);
				continue;
			}

			switch (std::get<DecisionAction>(outcome))
			{
			case DecisionAction::Merge:
				return MergeDirectory{ mv(proposed) };
			case DecisionAction::Skip:
				return SkipNode{};
			default:
				assert_debug_only(std::get<DecisionAction>(outcome) == DecisionAction::Cancel);
				return CancelOperation{};
			}
		}

		// Any non-directory entry - including a directory link, which must never be merged through:
		// materializing into it would write inside its target.
		auto outcome = promptForCollision(context, OperationIssue{ IssueKind::TypeMismatch, source, destinationEntry, {} }, proposed);
		if (auto* respelled = std::get_if<CEntryPath>(&outcome))
		{
			proposed = mv(*respelled);
			continue;
		}

		if (std::get<DecisionAction>(outcome) == DecisionAction::Skip)
			return SkipNode{};
		assert_debug_only(std::get<DecisionAction>(outcome) == DecisionAction::Cancel);
		return CancelOperation{};
	}
}

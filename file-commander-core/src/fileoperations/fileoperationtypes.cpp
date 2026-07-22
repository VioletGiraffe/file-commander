#include "fileoperationtypes.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp" // mv()

namespace
{

// A panel's synthetic parent item ([..]) arrives as a path whose last component is "..". It is filtered
// on the raw text: parsing would silently collapse it into the parent directory instead.
bool isSyntheticParentEntry(const QString& trimmedRawPath)
{
	return trimmedRawPath == QLatin1String("..")
		|| trimmedRawPath.endsWith(QLatin1String("/.."))
		|| trimmedRawPath.endsWith(QLatin1String("\\.."));
}

std::expected<std::vector<CEntryPath>, RequestValidationError> parseSourcePaths(const QStringList& rawSourcePaths)
{
	std::vector<CEntryPath> sources;
	sources.reserve(static_cast<size_t>(rawSourcePaths.size()));
	for (const QString& rawPath : rawSourcePaths)
	{
		if (isSyntheticParentEntry(rawPath.trimmed()))
			continue;

		auto parsed = parseOperationPath(rawPath);
		if (!parsed)
			return std::unexpected{ RequestValidationError::InvalidPath };
		if (parsed->isRoot())
			return std::unexpected{ RequestValidationError::RootSource };

		sources.push_back(mv(*parsed));
	}

	if (sources.empty())
		return std::unexpected{ RequestValidationError::NoSources };
	return sources;
}

} // namespace

std::expected<TransferRequest, RequestValidationError> makeTransferRequest(
	const TransferKind kind, const QStringList& rawSourcePaths, const DestinationIntent intent, const QString& rawDestinationPath)
{
	auto sources = parseSourcePaths(rawSourcePaths);
	if (!sources)
		return std::unexpected{ sources.error() };

	if (intent == DestinationIntent::ExactEntry && sources->size() != 1)
		return std::unexpected{ RequestValidationError::ExactEntryRequiresSingleSource };

	auto destination = parseOperationPath(rawDestinationPath);
	if (!destination)
		return std::unexpected{ RequestValidationError::InvalidPath };
	// A root can receive entries (IntoDirectory) but cannot itself be a proposed entry: it has no parent
	// for the resolver's rename loop and could never be published over.
	if (intent == DestinationIntent::ExactEntry && destination->isRoot())
		return std::unexpected{ RequestValidationError::InvalidPath };

	return TransferRequest{ kind, mv(*sources), DestinationSpec{ intent, mv(*destination) } };
}

std::expected<PermanentDeleteRequest, RequestValidationError> makePermanentDeleteRequest(const QStringList& rawSourcePaths)
{
	auto sources = parseSourcePaths(rawSourcePaths);
	if (!sources)
		return std::unexpected{ sources.error() };

	return PermanentDeleteRequest{ mv(*sources) };
}

std::vector<RootTransferIntent> rootTransferIntents(const TransferRequest& request)
{
	std::vector<RootTransferIntent> intents;
	intents.reserve(request.sources.size());

	if (request.destination.intent == DestinationIntent::ExactEntry)
	{
		assert_debug_only(request.sources.size() == 1);
		intents.push_back(RootTransferIntent{ request.sources.front(), request.destination.path });
		return intents;
	}

	for (const CEntryPath& source : request.sources)
		intents.push_back(RootTransferIntent{ source, request.destination.path.child(source.name()) });
	return intents;
}

NodeOutcome aggregateChildOutcome(const NodeOutcome aggregate, const NodeOutcome child) noexcept
{
	if (aggregate == NodeOutcome::Cancelled || child == NodeOutcome::Cancelled)
		return NodeOutcome::Cancelled;
	if (aggregate == NodeOutcome::Failed || child == NodeOutcome::Failed)
		return NodeOutcome::Failed;
	if (child == NodeOutcome::Skipped || child == NodeOutcome::Partial)
		return NodeOutcome::Partial;
	return aggregate;
}

AllowedActions allowedActionsFor(const IssueKind kind)
{
	using enum DecisionAction;
	switch (kind)
	{
	case IssueKind::FileReplacement:
		return { Replace, Rename, Skip, Cancel };
	case IssueKind::RootDirectoryMerge:
		return { Merge, Rename, Skip, Cancel };
	case IssueKind::TypeMismatch:
		return { Rename, Skip, Cancel };
	case IssueKind::ActionFailed:
		return { Retry, Skip, Cancel };
	case IssueKind::ReadOnlySourceRemoval:
		return { MakeWritable, Retry, Skip, Cancel };
	case IssueKind::UnsupportedEntry:
		return { Skip, Cancel };
	}

	assert_unconditional_r("Unknown IssueKind");
	return {};
}

bool isActionRememberable(const IssueKind kind, const DecisionAction action)
{
	using enum DecisionAction;
	switch (kind)
	{
	case IssueKind::FileReplacement:
		return action == Replace || action == Skip;
	case IssueKind::RootDirectoryMerge:
		return action == Merge || action == Skip;
	case IssueKind::TypeMismatch:
	case IssueKind::ActionFailed:
	case IssueKind::UnsupportedEntry:
		return action == Skip;
	case IssueKind::ReadOnlySourceRemoval:
		return action == MakeWritable || action == Skip;
	}

	assert_unconditional_r("Unknown IssueKind");
	return false;
}

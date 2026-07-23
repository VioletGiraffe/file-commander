// WP3: typed requests and factories, the six-row normative decision table, remembered decisions in the
// execution context, and the destination resolver's complete collision matrix.

#include "fileoperations/cdestinationresolver.h"
#include "fileoperations/coperationexecutioncontext.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

#ifndef _WIN32
#include <errno.h>
#endif

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

// An absolute path that exists only as text - factory and policy tests never touch the filesystem.
QString abstractPath(const char* tail)
{
#ifdef _WIN32
	return QStringLiteral("C:/") % QLatin1String(tail);
#else
	return QStringLiteral("/") % QLatin1String(tail);
#endif
}

QString rootPath()
{
#ifdef _WIN32
	return QStringLiteral("C:\\");
#else
	return QStringLiteral("/");
#endif
}

EntrySnapshot fakeSnapshot(const OperationEntryKind kind, const char* tail)
{
	return EntrySnapshot{ ep(abstractPath(tail)), kind, 0 };
}

// Scripted decision provider: hands out pre-programmed decisions in order and records every request.
struct ScriptedDecisions
{
	std::vector<Decision> script;
	std::vector<DecisionRequest> seenRequests;
	size_t nextIndex = 0;
	bool cancelInsteadOfAnswering = false;

	std::optional<Decision> answer(const DecisionRequest& request)
	{
		seenRequests.push_back(request);
		if (cancelInsteadOfAnswering)
			return {};
		REQUIRE(nextIndex < script.size());
		return script[nextIndex++];
	}
};

COperationExecutionContext scriptedContext(ScriptedDecisions& decisions)
{
	return COperationExecutionContext{
		PrimaryProgressUnit::Bytes,
		[] { return true; },
		[&decisions](const DecisionRequest& request) { return decisions.answer(request); },
		[](const ProgressSnapshot&) {}
	};
}

Decision renameTo(const QString& newName)
{
	return Decision{ DecisionAction::Rename, DecisionScope::ThisItem, newName };
}

} // namespace

//
// Request factories
//

TEST_CASE("request factory: source filtering and validation", "[requests]")
{
	const QString fileA = abstractPath("dir/a.bin");
	const QString fileB = abstractPath("dir/b.bin");
	const QString destination = abstractPath("dest");

	SECTION("valid multi-source into-directory request")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { fileA, fileB }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(request.has_value());
		REQUIRE(request->sources.size() == 2);
		CHECK(request->sources[0].value() == fileA);
		CHECK(request->kind == TransferKind::Copy);
		CHECK(request->destination.intent == DestinationIntent::IntoDirectory);
	}

	SECTION("empty source list")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, {}, DestinationIntent::IntoDirectory, destination);
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::NoSources);
	}

	SECTION("the synthetic parent entry is filtered once")
	{
		const auto request = makeTransferRequest(TransferKind::Copy,
			{ abstractPath("dir/.."), fileA, QStringLiteral("..") }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(request.has_value());
		REQUIRE(request->sources.size() == 1);
		CHECK(request->sources[0].value() == fileA);
	}

	SECTION("a selection reduced to nothing by the parent filter is NoSources")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { abstractPath("dir/..") }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::NoSources);
	}

	SECTION("invalid source text")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { QStringLiteral("relative/path") }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::InvalidPath);
	}

	SECTION("a filesystem root is not a valid source")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { rootPath() }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::RootSource);
	}

	SECTION("exact-entry intent requires a single source")
	{
		const auto request = makeTransferRequest(TransferKind::Move, { fileA, fileB }, DestinationIntent::ExactEntry, destination);
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::ExactEntryRequiresSingleSource);
	}

	SECTION("exact-entry intent counts sources after the parent filter")
	{
		const auto request = makeTransferRequest(TransferKind::Move,
			{ abstractPath("dir/.."), fileA }, DestinationIntent::ExactEntry, destination);
		REQUIRE(request.has_value());
		REQUIRE(request->sources.size() == 1);
		CHECK(request->sources[0].value() == fileA);
	}

#ifdef _WIN32
	SECTION("the synthetic parent entry is filtered in its backslash spelling too")
	{
		const auto request = makeTransferRequest(TransferKind::Copy,
			{ QStringLiteral("C:\\dir\\.."), fileA }, DestinationIntent::IntoDirectory, destination);
		REQUIRE(request.has_value());
		REQUIRE(request->sources.size() == 1);
		CHECK(request->sources[0].value() == fileA);
	}
#endif

	SECTION("exact-entry destination cannot be a root")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { fileA }, DestinationIntent::ExactEntry, rootPath());
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::InvalidPath);
	}

	SECTION("invalid destination text")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { fileA }, DestinationIntent::IntoDirectory, QStringLiteral("  "));
		REQUIRE(!request.has_value());
		CHECK(request.error() == RequestValidationError::InvalidPath);
	}

	SECTION("delete request applies the same source rules")
	{
		CHECK(makePermanentDeleteRequest({}).error() == RequestValidationError::NoSources);
		CHECK(makePermanentDeleteRequest({ abstractPath("dir/..") }).error() == RequestValidationError::NoSources);
		CHECK(makePermanentDeleteRequest({ rootPath() }).error() == RequestValidationError::RootSource);

		const auto request = makePermanentDeleteRequest({ fileA, fileB });
		REQUIRE(request.has_value());
		CHECK(request->sources.size() == 2);
	}
}

TEST_CASE("request factory: root transfer intents", "[requests]")
{
	SECTION("into-directory maps each source to destination/name")
	{
		const auto request = makeTransferRequest(TransferKind::Copy,
			{ abstractPath("src/a.bin"), abstractPath("elsewhere/sub") }, DestinationIntent::IntoDirectory, abstractPath("dest"));
		REQUIRE(request.has_value());

		const auto intents = rootTransferIntents(*request);
		REQUIRE(intents.size() == 2);
		CHECK(intents[0].source.value() == abstractPath("src/a.bin"));
		CHECK(intents[0].proposedDestination.value() == abstractPath("dest/a.bin"));
		CHECK(intents[1].proposedDestination.value() == abstractPath("dest/sub"));
	}

	SECTION("into a root destination")
	{
		const auto request = makeTransferRequest(TransferKind::Copy, { abstractPath("src/a.bin") }, DestinationIntent::IntoDirectory, rootPath());
		REQUIRE(request.has_value());

		const auto intents = rootTransferIntents(*request);
		REQUIRE(intents.size() == 1);
		CHECK(intents[0].proposedDestination.value() == abstractPath("a.bin"));
	}

	SECTION("exact entry maps the single source to the exact path")
	{
		const auto request = makeTransferRequest(TransferKind::Move,
			{ abstractPath("src/a.bin") }, DestinationIntent::ExactEntry, abstractPath("dest/renamed.bin"));
		REQUIRE(request.has_value());

		const auto intents = rootTransferIntents(*request);
		REQUIRE(intents.size() == 1);
		CHECK(intents[0].proposedDestination.value() == abstractPath("dest/renamed.bin"));
	}
}

//
// The normative policy table
//

TEST_CASE("the six-row policy table is exhaustive and exact", "[decisions]")
{
	using enum DecisionAction;

	const struct
	{
		IssueKind kind;
		AllowedActions allowed;
		std::vector<DecisionAction> rememberable;
	} rows[]{
		{ IssueKind::FileReplacement, { Replace, Rename, Skip, Cancel }, { Replace, Skip } },
		{ IssueKind::RootDirectoryMerge, { Merge, Rename, Skip, Cancel }, { Merge, Skip } },
		{ IssueKind::TypeMismatch, { Rename, Skip, Cancel }, { Skip } },
		{ IssueKind::ActionFailed, { Retry, Skip, Cancel }, { Skip } },
		{ IssueKind::ReadOnlySourceRemoval, { MakeWritable, Retry, Skip, Cancel }, { MakeWritable, Skip } },
		{ IssueKind::UnsupportedEntry, { Skip, Cancel }, { Skip } },
	};
	constexpr DecisionAction allActions[]{ Skip, Replace, Merge, MakeWritable, Rename, Retry, Cancel };

	for (const auto& row : rows)
	{
		INFO("IssueKind " << static_cast<int>(row.kind));
		CHECK(allowedActionsFor(row.kind) == row.allowed);

		for (const DecisionAction action : allActions)
		{
			const bool expected = std::find(row.rememberable.begin(), row.rememberable.end(), action) != row.rememberable.end();
			CHECK(isActionRememberable(row.kind, action) == expected);
		}
	}
}

//
// Remembered decisions in the execution context
//

TEST_CASE("remembered decisions: storage, replay, and isolation", "[decisions]")
{
	const OperationIssue replacementIssue{ IssueKind::FileReplacement,
		fakeSnapshot(OperationEntryKind::RegularFile, "src/a.bin"), fakeSnapshot(OperationEntryKind::RegularFile, "dest/a.bin"), {} };
	const OperationIssue mismatchIssue{ IssueKind::TypeMismatch,
		fakeSnapshot(OperationEntryKind::RegularFile, "src/b.bin"), fakeSnapshot(OperationEntryKind::Directory, "dest/b"), {} };

	SECTION("an All response of a rememberable action replays without prompting")
	{
		ScriptedDecisions decisions{ .script = { act(DecisionAction::Replace, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		const auto first = context.resolveDecision(replacementIssue);
		REQUIRE(first.has_value());
		CHECK(first->action == DecisionAction::Replace);
		CHECK(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].remainingMatchingScopeAllowed);
		CHECK(decisions.seenRequests[0].allowedActions == allowedActionsFor(IssueKind::FileReplacement));

		const auto second = context.resolveDecision(replacementIssue);
		REQUIRE(second.has_value());
		CHECK(second->action == DecisionAction::Replace);
		CHECK(second->scope == DecisionScope::RemainingMatchingIssues);
		CHECK(decisions.seenRequests.size() == 1); // Answered from the remembered table
	}

	SECTION("remembered answers are isolated per issue kind")
	{
		ScriptedDecisions decisions{ .script = {
			act(DecisionAction::Replace, DecisionScope::RemainingMatchingIssues),
			act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(replacementIssue).has_value());
		const auto mismatch = context.resolveDecision(mismatchIssue);
		REQUIRE(mismatch.has_value());
		CHECK(mismatch->action == DecisionAction::Skip); // The remembered Replace did not answer a different question
		CHECK(decisions.seenRequests.size() == 2);
	}

	SECTION("one remembered ActionFailed Skip spans different failed actions")
	{
		const OperationIssue writeFailure{ IssueKind::ActionFailed, fakeSnapshot(OperationEntryKind::RegularFile, "src/a.bin"), {},
			FailureDetails{ FailedAction::WriteDestination, CFileSystemError{ FileErrorCategory::IoFailure, 0, QStringLiteral("io") } } };
		const OperationIssue removeFailure{ IssueKind::ActionFailed, fakeSnapshot(OperationEntryKind::RegularFile, "src/b.bin"), {},
			FailureDetails{ FailedAction::RemoveEntry, CFileSystemError{ FileErrorCategory::PermissionDenied, 0, QStringLiteral("denied") } } };

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(writeFailure).has_value());
		const auto second = context.resolveDecision(removeFailure);
		REQUIRE(second.has_value());
		CHECK(second->action == DecisionAction::Skip);
		CHECK(decisions.seenRequests.size() == 1);
	}

	SECTION("a remembered MakeWritable answers the remaining read-only removals")
	{
		const OperationIssue readOnlyIssue{ IssueKind::ReadOnlySourceRemoval,
			fakeSnapshot(OperationEntryKind::RegularFile, "src/ro1.bin"), {}, {} };
		const OperationIssue secondReadOnlyIssue{ IssueKind::ReadOnlySourceRemoval,
			fakeSnapshot(OperationEntryKind::RegularFile, "src/ro2.bin"), {}, {} };

		ScriptedDecisions decisions{ .script = { act(DecisionAction::MakeWritable, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(readOnlyIssue).has_value());
		const auto second = context.resolveDecision(secondReadOnlyIssue);
		REQUIRE(second.has_value());
		CHECK(second->action == DecisionAction::MakeWritable);
		CHECK(decisions.seenRequests.size() == 1);
	}

	SECTION("a remembered TypeMismatch Skip answers the remaining mismatches")
	{
		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(mismatchIssue).has_value());
		const OperationIssue secondMismatch{ IssueKind::TypeMismatch,
			fakeSnapshot(OperationEntryKind::Directory, "src/c"), fakeSnapshot(OperationEntryKind::RegularFile, "dest/c.bin"), {} };
		const auto second = context.resolveDecision(secondMismatch);
		REQUIRE(second.has_value());
		CHECK(second->action == DecisionAction::Skip);
		CHECK(decisions.seenRequests.size() == 1);
	}

	SECTION("a non-rememberable action with All scope is recorded for this item only")
	{
		ScriptedDecisions decisions{ .script = {
			Decision{ DecisionAction::Rename, DecisionScope::RemainingMatchingIssues, QStringLiteral("other.bin") },
			act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		const auto first = context.resolveDecision(replacementIssue);
		REQUIRE(first.has_value());
		CHECK(first->action == DecisionAction::Rename);
		CHECK(first->scope == DecisionScope::ThisItem); // The illegal All was voided

		const auto second = context.resolveDecision(replacementIssue);
		REQUIRE(second.has_value());
		CHECK(second->action == DecisionAction::Skip);
		CHECK(decisions.seenRequests.size() == 2); // Nothing was remembered
	}

	SECTION("a scope-disallowed request neither consults nor updates remembered answers")
	{
		ScriptedDecisions decisions{ .script = {
			act(DecisionAction::Replace, DecisionScope::RemainingMatchingIssues),
			act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(replacementIssue).has_value()); // Remembers Replace

		const auto isolated = context.resolveDecision(replacementIssue, false);
		REQUIRE(isolated.has_value());
		CHECK(isolated->action == DecisionAction::Skip); // Prompted despite the remembered Replace
		CHECK(isolated->scope == DecisionScope::ThisItem); // And its All response was voided
		CHECK(decisions.seenRequests.size() == 2);
		CHECK(!decisions.seenRequests[1].remainingMatchingScopeAllowed);

		const auto replay = context.resolveDecision(replacementIssue);
		REQUIRE(replay.has_value());
		CHECK(replay->action == DecisionAction::Replace); // The remembered answer survived unchanged
		CHECK(decisions.seenRequests.size() == 2);
	}

	SECTION("cancellation overrides the decision")
	{
		ScriptedDecisions decisions{ .cancelInsteadOfAnswering = true };
		auto context = scriptedContext(decisions);
		CHECK(!context.resolveDecision(replacementIssue).has_value());
	}

	SECTION("UnsupportedEntry offers Skip and Cancel and remembers Skip")
	{
		const OperationIssue unsupported{ IssueKind::UnsupportedEntry, fakeSnapshot(OperationEntryKind::Other, "src/pipe"), {}, {} };

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		REQUIRE(context.resolveDecision(unsupported).has_value());
		CHECK(decisions.seenRequests[0].allowedActions == AllowedActions{ DecisionAction::Skip, DecisionAction::Cancel });

		const auto replay = context.resolveDecision(unsupported);
		REQUIRE(replay.has_value());
		CHECK(replay->action == DecisionAction::Skip);
		CHECK(decisions.seenRequests.size() == 1);
	}
}

TEST_CASE("execution context: summary accumulation is bounded", "[decisions]")
{
	ScriptedDecisions decisions;
	auto context = scriptedContext(decisions);

	const OperationDiagnostic diagnostic{
		FailureDetails{ FailedAction::CleanupStaging, CFileSystemError{ FileErrorCategory::IoFailure, 0, QStringLiteral("w") } },
		fakeSnapshot(OperationEntryKind::RegularFile, "src/a.bin"), {} };

	for (int i = 0; i < 20; ++i)
		context.recordWarning(diagnostic);
	context.recordFailure(diagnostic);
	context.addCompletedItems(2);
	context.addSkippedItems(1);
	context.addAlreadySatisfiedItems(1);
	context.addTransferredBytes(1234);

	const auto summary = context.makeSummary(CompletionStatus::Failed);
	CHECK(summary.status == CompletionStatus::Failed);
	CHECK(summary.warningCount == 20);
	CHECK(summary.representativeWarnings.size() == 16);
	CHECK(summary.failedItems == 1);
	CHECK(summary.representativeFailures.size() == 1);
	CHECK(summary.completedItems == 2);
	CHECK(summary.skippedItems == 1);
	CHECK(summary.alreadySatisfiedItems == 1);
	CHECK(summary.transferredBytes == 1234);
}

//
// The destination resolver matrix
//

TEST_CASE("file resolver: absent, same object, and file collisions", "[resolver]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", QByteArray{ "DATA" });
	const EntrySnapshot source = snapshotOf(base % "/source.bin");

	SECTION("absent destination is used directly, silently")
	{
		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/new.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->path.value() == ep(base % "/new.bin").value());
		CHECK(use->replacement == ReplacementMode::RequireAbsent);
		CHECK(decisions.seenRequests.empty());
	}

	SECTION("the source itself as destination is already satisfied, silently")
	{
		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/source.bin"));
		CHECK(std::holds_alternative<AlreadySatisfied>(choice));
		CHECK(decisions.seenRequests.empty());
	}

	SECTION("a hard-link alias of the source is the same object")
	{
		REQUIRE(createHardLink(base % "/source.bin", base % "/alias.bin"));

		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/alias.bin"));
		CHECK(std::holds_alternative<AlreadySatisfied>(choice));
		CHECK(decisions.seenRequests.empty());
	}

	SECTION("file collision prompts with the full replacement row")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Replace) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/taken.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->replacement == ReplacementMode::ReplaceExistingFile);

		REQUIRE(decisions.seenRequests.size() == 1);
		const DecisionRequest& request = decisions.seenRequests[0];
		CHECK(request.issue.kind == IssueKind::FileReplacement);
		CHECK(request.allowedActions == allowedActionsFor(IssueKind::FileReplacement));
		REQUIRE(request.issue.destination.has_value());
		CHECK(request.issue.destination->kind == OperationEntryKind::RegularFile);
	}

	SECTION("Skip, Cancel, and cancellation-override outcomes")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		ScriptedDecisions skipDecisions{ .script = { act(DecisionAction::Skip) } };
		auto skipContext = scriptedContext(skipDecisions);
		CHECK(std::holds_alternative<SkipNode>(resolveFileDestination(skipContext, source, ep(base % "/taken.bin"))));

		ScriptedDecisions cancelDecisions{ .script = { act(DecisionAction::Cancel) } };
		auto cancelContext = scriptedContext(cancelDecisions);
		CHECK(std::holds_alternative<CancelOperation>(resolveFileDestination(cancelContext, source, ep(base % "/taken.bin"))));

		ScriptedDecisions overridden{ .cancelInsteadOfAnswering = true };
		auto overriddenContext = scriptedContext(overridden);
		CHECK(std::holds_alternative<CancelOperation>(resolveFileDestination(overriddenContext, source, ep(base % "/taken.bin"))));
	}

	SECTION("directory destination is a type mismatch without Replace")
	{
		REQUIRE(QDir{}.mkpath(base % "/subdir"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/subdir"));
		CHECK(std::holds_alternative<SkipNode>(choice));

		REQUIRE(decisions.seenRequests.size() == 1);
		const DecisionRequest& request = decisions.seenRequests[0];
		CHECK(request.issue.kind == IssueKind::TypeMismatch);
		CHECK(request.allowedActions == allowedActionsFor(IssueKind::TypeMismatch));
		CHECK(request.issue.source.kind == OperationEntryKind::RegularFile); // Both kinds present for the wording
		REQUIRE(request.issue.destination.has_value());
		CHECK(request.issue.destination->kind == OperationEntryKind::Directory);
	}
}

TEST_CASE("file resolver: the rename loop", "[resolver]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", QByteArray{ "DATA" });
	writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });
	const EntrySnapshot source = snapshotOf(base % "/source.bin");

	SECTION("rename to an absent name")
	{
		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("fresh.bin")) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/taken.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->path.value() == ep(base % "/fresh.bin").value());
		CHECK(use->replacement == ReplacementMode::RequireAbsent);
		CHECK(decisions.seenRequests.size() == 1);
	}

	SECTION("rename onto another existing file re-enters the matrix")
	{
		writeTestFile(base % "/taken2.bin", QByteArray{ "OLD2" });

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("taken2.bin")), act(DecisionAction::Replace) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/taken.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->path.value() == ep(base % "/taken2.bin").value());
		CHECK(use->replacement == ReplacementMode::ReplaceExistingFile);
		REQUIRE(decisions.seenRequests.size() == 2);
		CHECK(decisions.seenRequests[1].issue.kind == IssueKind::FileReplacement);
	}

	SECTION("rename onto a directory becomes a type mismatch")
	{
		REQUIRE(QDir{}.mkpath(base % "/subdir"));

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("subdir")), act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(resolveFileDestination(context, source, ep(base % "/taken.bin"))));
		REQUIRE(decisions.seenRequests.size() == 2);
		CHECK(decisions.seenRequests[1].issue.kind == IssueKind::TypeMismatch);
	}

	SECTION("rename onto the source's own alias is already satisfied")
	{
		REQUIRE(createHardLink(base % "/source.bin", base % "/alias.bin"));

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("alias.bin")) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<AlreadySatisfied>(resolveFileDestination(context, source, ep(base % "/taken.bin"))));
	}

	SECTION("invalid rename input re-asks instead of failing")
	{
		ScriptedDecisions decisions{ .script = {
			renameTo(QString{}),
			renameTo(QStringLiteral("bad/name")),
			Decision{ DecisionAction::Rename, DecisionScope::ThisItem, {} }, // No name at all
			act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(resolveFileDestination(context, source, ep(base % "/taken.bin"))));
		CHECK(decisions.seenRequests.size() == 4);
	}
}

TEST_CASE("file resolver: destination links are entries", "[resolver][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", QByteArray{ "DATA" });
	const EntrySnapshot source = snapshotOf(base % "/source.bin");

	SECTION("a directory link destination is a replaceable entry, not a directory")
	{
		REQUIRE(QDir{}.mkpath(base % "/link-target"));
		REQUIRE(createDirectoryLink(base % "/link-target", base % "/dirlink"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Replace) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/dirlink"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->replacement == ReplacementMode::ReplaceExistingFile);

		REQUIRE(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].issue.kind == IssueKind::FileReplacement);
		CHECK(decisions.seenRequests[0].issue.destination->kind == OperationEntryKind::DirectoryLink);
	}

#ifndef _WIN32
	SECTION("a file symlink destination is a file-like collision")
	{
		writeTestFile(base % "/target.bin", QByteArray{ "T" });
		REQUIRE(QFile::link(base % "/target.bin", base % "/filelink.bin"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Replace) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/filelink.bin"));
		REQUIRE(std::get_if<UseDestination>(&choice) != nullptr);
		CHECK(decisions.seenRequests[0].issue.kind == IssueKind::FileReplacement);
		CHECK(decisions.seenRequests[0].issue.destination->kind == OperationEntryKind::FileLink);
	}

	SECTION("a file symlink source resolves like a file")
	{
		writeTestFile(base % "/target.bin", QByteArray{ "T" });
		REQUIRE(QFile::link(base % "/target.bin", base % "/linksource.bin"));
		const EntrySnapshot linkSource = snapshotOf(base % "/linksource.bin");
		REQUIRE(linkSource.kind == OperationEntryKind::FileLink);

		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, linkSource, ep(base % "/new.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->replacement == ReplacementMode::RequireAbsent);
		CHECK(decisions.seenRequests.empty());
	}
#endif
}

TEST_CASE("directory resolver: absent, merge positions, and mismatches", "[resolver]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/srcdir"));
	const EntrySnapshot source = snapshotOf(base % "/srcdir");

	SECTION("absent destination is used directly")
	{
		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, source, ep(base % "/newdir"), TransferNodePosition::SelectedRoot);
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->replacement == ReplacementMode::RequireAbsent);
		CHECK(decisions.seenRequests.empty());
	}

	SECTION("the directory itself as destination is already satisfied")
	{
		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);
		CHECK(std::holds_alternative<AlreadySatisfied>(
			resolveDirectoryDestination(context, source, ep(base % "/srcdir"), TransferNodePosition::SelectedRoot)));
	}

	SECTION("selected-root directory collision prompts for merge")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Merge) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot);
		const auto* merge = std::get_if<MergeDirectory>(&choice);
		REQUIRE(merge != nullptr);
		CHECK(merge->path.value() == ep(base % "/existing").value());

		REQUIRE(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);
		CHECK(decisions.seenRequests[0].allowedActions == allowedActionsFor(IssueKind::RootDirectoryMerge));
	}

	SECTION("a selected-root collision skips, cancels, and honors the cancellation override")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing"));

		ScriptedDecisions skipDecisions{ .script = { act(DecisionAction::Skip) } };
		auto skipContext = scriptedContext(skipDecisions);
		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(skipContext, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
		REQUIRE(skipDecisions.seenRequests.size() == 1);
		CHECK(skipDecisions.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);

		ScriptedDecisions cancelDecisions{ .script = { act(DecisionAction::Cancel) } };
		auto cancelContext = scriptedContext(cancelDecisions);
		CHECK(std::holds_alternative<CancelOperation>(
			resolveDirectoryDestination(cancelContext, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));

		ScriptedDecisions overridden{ .cancelInsteadOfAnswering = true };
		auto overriddenContext = scriptedContext(overridden);
		CHECK(std::holds_alternative<CancelOperation>(
			resolveDirectoryDestination(overriddenContext, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
	}

	SECTION("Merge All remembers for the remaining selected-root collisions")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing"));
		REQUIRE(QDir{}.mkpath(base % "/existing2"));
		REQUIRE(QDir{}.mkpath(base % "/srcdir2"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Merge, DecisionScope::RemainingMatchingIssues) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<MergeDirectory>(
			resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
		CHECK(std::holds_alternative<MergeDirectory>(
			resolveDirectoryDestination(context, snapshotOf(base % "/srcdir2"), ep(base % "/existing2"), TransferNodePosition::SelectedRoot)));
		CHECK(decisions.seenRequests.size() == 1); // The second collision was answered from the remembered Merge
	}

	SECTION("a descendant directory collision merges silently")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing"));

		ScriptedDecisions decisions;
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::Descendant);
		CHECK(std::holds_alternative<MergeDirectory>(choice));
		CHECK(decisions.seenRequests.empty());
	}

	SECTION("a file destination is a type mismatch without Replace or Merge")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(context, source, ep(base % "/taken.bin"), TransferNodePosition::SelectedRoot)));

		REQUIRE(decisions.seenRequests.size() == 1);
		const DecisionRequest& request = decisions.seenRequests[0];
		CHECK(request.issue.kind == IssueKind::TypeMismatch);
		CHECK(request.issue.source.kind == OperationEntryKind::Directory);
		CHECK(request.issue.destination->kind == OperationEntryKind::RegularFile);
	}

	SECTION("a type mismatch cancels, directly or via the cancellation override")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		ScriptedDecisions cancelDecisions{ .script = { act(DecisionAction::Cancel) } };
		auto cancelContext = scriptedContext(cancelDecisions);
		CHECK(std::holds_alternative<CancelOperation>(
			resolveDirectoryDestination(cancelContext, source, ep(base % "/taken.bin"), TransferNodePosition::SelectedRoot)));
		CHECK(cancelDecisions.seenRequests[0].issue.kind == IssueKind::TypeMismatch);

		ScriptedDecisions overridden{ .cancelInsteadOfAnswering = true };
		auto overriddenContext = scriptedContext(overridden);
		CHECK(std::holds_alternative<CancelOperation>(
			resolveDirectoryDestination(overriddenContext, source, ep(base % "/taken.bin"), TransferNodePosition::SelectedRoot)));
	}

#ifndef _WIN32
	SECTION("a file symlink destination is a type mismatch as a FileLink, never followed")
	{
		writeTestFile(base % "/target.bin", QByteArray{ "T" });
		REQUIRE(QFile::link(base % "/target.bin", base % "/filelink.bin"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(context, source, ep(base % "/filelink.bin"), TransferNodePosition::SelectedRoot)));

		REQUIRE(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].issue.kind == IssueKind::TypeMismatch);
		CHECK(decisions.seenRequests[0].issue.destination->kind == OperationEntryKind::FileLink);
	}
#endif

	SECTION("a directory link destination is never merged through")
	{
		REQUIRE(QDir{}.mkpath(base % "/link-target"));
		REQUIRE(createDirectoryLink(base % "/link-target", base % "/dirlink"));

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(context, source, ep(base % "/dirlink"), TransferNodePosition::SelectedRoot)));

		REQUIRE(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].issue.kind == IssueKind::TypeMismatch);
		CHECK(decisions.seenRequests[0].issue.destination->kind == OperationEntryKind::DirectoryLink);
	}

	SECTION("rename resolves a selected-root collision to a fresh directory")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing"));

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("renamed-dir")) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot);
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->path.value() == ep(base % "/renamed-dir").value());
		CHECK(use->replacement == ReplacementMode::RequireAbsent);
	}
}

TEST_CASE("directory resolver: the rename loop", "[resolver]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/srcdir"));
	REQUIRE(QDir{}.mkpath(base % "/existing"));
	const EntrySnapshot source = snapshotOf(base % "/srcdir");

	SECTION("invalid rename input re-asks instead of failing")
	{
		ScriptedDecisions decisions{ .script = {
			renameTo(QString{}),
			renameTo(QStringLiteral("bad/name")),
			act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
		CHECK(decisions.seenRequests.size() == 3);
	}

	SECTION("rename onto another existing directory re-enters as a merge question")
	{
		REQUIRE(QDir{}.mkpath(base % "/existing2"));

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("existing2")), act(DecisionAction::Merge) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot);
		const auto* merge = std::get_if<MergeDirectory>(&choice);
		REQUIRE(merge != nullptr);
		CHECK(merge->path.value() == ep(base % "/existing2").value());
		REQUIRE(decisions.seenRequests.size() == 2);
		CHECK(decisions.seenRequests[1].issue.kind == IssueKind::RootDirectoryMerge);
	}

	SECTION("rename onto a file becomes a type mismatch")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("taken.bin")), act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<SkipNode>(
			resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
		REQUIRE(decisions.seenRequests.size() == 2);
		CHECK(decisions.seenRequests[1].issue.kind == IssueKind::TypeMismatch);
	}

	SECTION("rename onto the source itself is already satisfied")
	{
		ScriptedDecisions decisions{ .script = { renameTo(QStringLiteral("srcdir")) } };
		auto context = scriptedContext(decisions);

		CHECK(std::holds_alternative<AlreadySatisfied>(
			resolveDirectoryDestination(context, source, ep(base % "/existing"), TransferNodePosition::SelectedRoot)));
		CHECK(decisions.seenRequests.size() == 1);
	}
}

TEST_CASE("resolver: a destination inspection failure prompts ActionFailed", "[resolver]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/source.bin", QByteArray{ "DATA" });
	const EntrySnapshot source = snapshotOf(base % "/source.bin");

#ifdef _WIN32
	constexpr NativeErrorCode inspectFailureCode = ERROR_ACCESS_DENIED;
#else
	constexpr NativeErrorCode inspectFailureCode = EACCES;
#endif

	SECTION("Retry re-inspects and resolution proceeds")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::InspectEntry_Native, inspectFailureCode);

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Retry) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveFileDestination(context, source, ep(base % "/new.bin"));
		const auto* use = std::get_if<UseDestination>(&choice);
		REQUIRE(use != nullptr);
		CHECK(use->replacement == ReplacementMode::RequireAbsent);

		REQUIRE(decisions.seenRequests.size() == 1);
		const DecisionRequest& request = decisions.seenRequests[0];
		CHECK(request.issue.kind == IssueKind::ActionFailed);
		CHECK(request.allowedActions == allowedActionsFor(IssueKind::ActionFailed));
		REQUIRE(request.issue.failure.has_value());
		CHECK(request.issue.failure->action == FailedAction::InspectDestination);
		CHECK(request.issue.failure->filesystemError.category == FileErrorCategory::PermissionDenied);
	}

	SECTION("Skip abandons the node")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::InspectEntry_Native, inspectFailureCode);

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Skip) } };
		auto context = scriptedContext(decisions);
		CHECK(std::holds_alternative<SkipNode>(resolveFileDestination(context, source, ep(base % "/new.bin"))));
	}

	SECTION("Cancel and the cancellation override end the operation")
	{
		{
			CFaultHookScope hooks;
			hooks.forceNativeError(Point::InspectEntry_Native, inspectFailureCode);

			ScriptedDecisions decisions{ .script = { act(DecisionAction::Cancel) } };
			auto context = scriptedContext(decisions);
			CHECK(std::holds_alternative<CancelOperation>(resolveFileDestination(context, source, ep(base % "/new.bin"))));
		}
		{
			CFaultHookScope hooks;
			hooks.forceNativeError(Point::InspectEntry_Native, inspectFailureCode);

			ScriptedDecisions overridden{ .cancelInsteadOfAnswering = true };
			auto context = scriptedContext(overridden);
			CHECK(std::holds_alternative<CancelOperation>(resolveFileDestination(context, source, ep(base % "/new.bin"))));
		}
	}

	SECTION("the directory resolver shares the same failure path")
	{
		REQUIRE(QDir{}.mkpath(base % "/srcdir"));
		const EntrySnapshot dirSource = snapshotOf(base % "/srcdir"); // Snapshotted before the scope: only the resolver's inspect may consume the forced error

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::InspectEntry_Native, inspectFailureCode);

		ScriptedDecisions decisions{ .script = { act(DecisionAction::Retry) } };
		auto context = scriptedContext(decisions);

		const auto choice = resolveDirectoryDestination(context, dirSource, ep(base % "/newdir"), TransferNodePosition::SelectedRoot);
		REQUIRE(std::get_if<UseDestination>(&choice) != nullptr);
		REQUIRE(decisions.seenRequests.size() == 1);
		CHECK(decisions.seenRequests[0].issue.failure->action == FailedAction::InspectDestination);
	}
}

TEST_CASE("into-directory mapping happens before collision classification", "[resolver][requests]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// Copying a file into an existing directory is not a collision with the directory itself:
	// the mapped proposed entry D/a.bin decides, and here it collides with a file.
	writeTestFile(base % "/a.bin", QByteArray{ "SRC" });
	REQUIRE(QDir{}.mkpath(base % "/dest"));
	writeTestFile(base % "/dest/a.bin", QByteArray{ "OLD" });

	const auto request = makeTransferRequest(TransferKind::Copy, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest");
	REQUIRE(request.has_value());
	const auto intents = rootTransferIntents(*request);
	REQUIRE(intents.size() == 1);

	ScriptedDecisions decisions{ .script = { act(DecisionAction::Replace) } };
	auto context = scriptedContext(decisions);

	const auto choice = resolveFileDestination(context, snapshotOf(base % "/a.bin"), intents[0].proposedDestination);
	const auto* use = std::get_if<UseDestination>(&choice);
	REQUIRE(use != nullptr);
	CHECK(use->path.value() == ep(base % "/dest/a.bin").value());
	CHECK(use->replacement == ReplacementMode::ReplaceExistingFile);
	CHECK(decisions.seenRequests[0].issue.kind == IssueKind::FileReplacement);
}

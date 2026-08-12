// WP5: the recursive copy executor - the composed behavior of resolver, tree builder, staged copy,
// context policy, outcome aggregation, accounting, timestamps, and progress.

#include "fileoperations/coperationexecutioncontext.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

// test_utils
#include "ctestfoldergenerator.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h> // mkfifo
#endif

#include <atomic>
#include <chrono>
#include <thread>

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode accessDeniedCode = ERROR_ACCESS_DENIED;
constexpr NativeErrorCode existsCode = ERROR_FILE_EXISTS;
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
constexpr NativeErrorCode diskFullCode = ERROR_DISK_FULL;
#else
constexpr NativeErrorCode accessDeniedCode = EACCES;
constexpr NativeErrorCode existsCode = EEXIST;
constexpr NativeErrorCode ioFailureCode = EIO;
constexpr NativeErrorCode diskFullCode = ENOSPC;
#endif

} // namespace

TEST_CASE("copy executor: files, trees, and multiple roots", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("exact-entry file copy")
	{
		const QByteArray contents = patternedContents(3000);
		writeTestFile(base % "/a.bin", contents);

		OperationScript script;
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(summary.transferredBytes == 3000);
		CHECK(readFileContents(base % "/copied.bin") == contents);
		CHECK(script.seenRequests.empty());
	}

	SECTION("exact-entry file copy silently creates missing destination parents")
	{
		const QByteArray contents = patternedContents(3000);
		writeTestFile(base % "/a.bin", contents);

		OperationScript script;
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/new/deep/copied.bin");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(readFileContents(base % "/new/deep/copied.bin") == contents);
		CHECK(script.seenRequests.empty());
	}

	SECTION("into-directory file copy silently creates the missing destination chain")
	{
		const QByteArray contents = patternedContents(3000);
		writeTestFile(base % "/a.bin", contents);

		OperationScript script;
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/new/deep");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(readFileContents(base % "/new/deep/a.bin") == contents);
		CHECK(script.seenRequests.empty());
	}

	SECTION("an occupied destination parent uses the directory-creation failure policy")
	{
		writeTestFile(base % "/a.bin", patternedContents(100));
		writeTestFile(base % "/occupied", QByteArray{ "OLD" });

		OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/occupied/copied.bin");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		CHECK(entryAbsent(base % "/occupied/copied.bin"));
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->action == FailedAction::CreateDestinationDirectory);
	}

	SECTION("empty and multi-chunk files")
	{
		writeTestFile(base % "/empty.bin", {});
		writeTestFile(base % "/large.bin", patternedContents(300'000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		OperationScript script;
		const auto summary = runCopy(script, { base % "/empty.bin", base % "/large.bin" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 2);
		CHECK(readFileContents(base % "/dest/empty.bin").isEmpty());
		CHECK(readFileContents(base % "/dest/large.bin") == patternedContents(300'000));
	}

	SECTION("a directory root and a file root together")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/sub"));
		writeTestFile(base % "/src/one.bin", patternedContents(100));
		writeTestFile(base % "/src/sub/two.bin", patternedContents(200));
		writeTestFile(base % "/solo.bin", patternedContents(300));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		OperationScript script;
		const auto summary = runCopy(script, { base % "/src", base % "/solo.bin" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 5); // src, one.bin, sub, two.bin, solo.bin
		CHECK(summary.transferredBytes == 600);
		requireEqualTrees(base % "/src", base % "/dest/src");
		CHECK(readFileContents(base % "/dest/solo.bin") == patternedContents(300));
		CHECK(stagingFileCount(base % "/dest") == 0);
	}
}

TEST_CASE("copy executor: random tree round-trip", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	INFO("Random seed: " << g_randomSeed);
	REQUIRE(QDir{}.mkpath(base % "/src"));

	CTestFolderGenerator generator;
	generator.setSeed(g_randomSeed);
	const auto tree = generator.generateRandomTree(base % "/src",
		{ .numFiles = 50, .numFolders = 12, .maxFileSize = 8000, .chunkSize = defaultTransferChunkSize });
	REQUIRE(tree.has_value());
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	// Nothing else here vouches for the generator, and every assertion below trusts the counts it reports.
	REQUIRE(countTreeEntries(base % "/src") == tree->files.size() + tree->folders.size());

	OperationScript script;
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == tree->files.size() + tree->folders.size() + 1); // Every entry plus the root itself
	CHECK(summary.transferredBytes == tree->totalBytes);
	CHECK(summary.skippedItems == 0);
	requireEqualTrees(base % "/src", base % "/dest/src");
}

TEST_CASE("copy executor: a tree whose paths exceed MAX_PATH", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	INFO("Random seed: " << g_randomSeed);
	REQUIRE(QDir{}.mkpath(base % "/src"));

	// Past Windows' 260-character limit, which only the \\?\ prefix gets beyond - and the destination side goes
	// further still, since the staging name a copy appends is longer than the final one.
	CTestFolderGenerator generator;
	generator.setSeed(g_randomSeed);
	const auto tree = generator.generateRandomTree(base % "/src",
		{ .numFiles = 8, .numFolders = 4, .maxFileSize = 2048, .minPathLength = 400 });
	REQUIRE(tree.has_value());
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == tree->files.size() + tree->folders.size() + 1);
	CHECK(summary.transferredBytes == tree->totalBytes);
	CHECK(script.seenRequests.empty());
	requireEqualTrees(base % "/src", base % "/dest/src");

	// Removed through the engine rather than left to QTemporaryDir: it covers the delete side of the same boundary,
	// and a tree left behind here would need the \\?\ prefix to delete at all.
	OperationScript cleanupScript;
	CHECK(runDelete(cleanupScript, { base % "/src", base % "/dest" }).status == CompletionStatus::Completed);
	CHECK(entryAbsent(base % "/src"));
	CHECK(entryAbsent(base % "/dest"));
}

TEST_CASE("copy executor: root rename rebases descendants", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeTestFile(base % "/src/sub/deep.bin", patternedContents(700));
	REQUIRE(QDir{}.mkpath(base % "/dest/src")); // The proposed root target collides

	OperationScript script{ .decisions = { Decision{ DecisionAction::Rename, DecisionScope::ThisItem, QStringLiteral("renamed") } } };
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(readFileContents(base % "/dest/renamed/sub/deep.bin") == patternedContents(700));
	CHECK(QDir{ base % "/dest/src" }.isEmpty()); // The colliding directory was left alone
}

TEST_CASE("copy executor: a file raced into a directory destination re-enters resolution", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/inside.bin", patternedContents(300));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	hooks.armBarrier(Point::CreateDirectory_FinalNative);

	OperationScript script{ .decisions = { Decision{ DecisionAction::Rename, DecisionScope::ThisItem, QStringLiteral("renamed") } } };
	const auto request = makeTransferRequest(TransferKind::Copy, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, defaultTransferChunkSize };

	OperationSummary summary;
	std::thread worker{ [&] { summary = executor.run(*request); } };

	REQUIRE(hooks.waitForBarrier(Point::CreateDirectory_FinalNative, std::chrono::milliseconds{ 5000 }));
	writeTestFile(base % "/dest/src", QByteArray{ "RACE" });
	hooks.releaseBarrier(Point::CreateDirectory_FinalNative);
	worker.join();

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 2);
	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::TypeMismatch);
	REQUIRE(script.seenRequests[0].issue.destination.has_value());
	CHECK(script.seenRequests[0].issue.destination->kind == OperationEntryKind::RegularFile);
	CHECK(readFileContents(base % "/dest/src") == QByteArray{ "RACE" });
	CHECK(readFileContents(base % "/dest/renamed/inside.bin") == patternedContents(300));
}

TEST_CASE("copy executor: a failed destination directory creation is skippable and retryable", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeTestFile(base % "/src/one.bin", patternedContents(100));
	writeTestFile(base % "/src/sub/two.bin", patternedContents(200));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	hooks.forceNativeError(Point::CreateDirectory_FinalNative, accessDeniedCode);

	SECTION("skip after the manifest exists accounts the whole subtree")
	{
		OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 4); // src, one.bin, sub, two.bin - the root's creation failed after the scan
		CHECK(summary.completedItems == 0);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->action == FailedAction::CreateDestinationDirectory);
		CHECK(entryAbsent(base % "/dest/src"));
	}

	SECTION("retry re-attempts the same directory")
	{
		OperationScript script{ .decisions = { act(DecisionAction::Retry) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 4);
		CHECK(hooks.arrivalCount(Point::CreateDirectory_FinalNative) == 3); // The failed root, its retry, then sub
		requireEqualTrees(base % "/src", base % "/dest/src");
	}
}

TEST_CASE("copy executor: skips leave siblings running and never demote completion", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	SECTION("skipping one colliding file")
	{
		REQUIRE(QDir{}.mkpath(base % "/src"));
		writeTestFile(base % "/src/colliding.bin", patternedContents(100));
		writeTestFile(base % "/src/free.bin", patternedContents(200));
		REQUIRE(QDir{}.mkpath(base % "/dest/src"));
		writeTestFile(base % "/dest/src/colliding.bin", QByteArray{ "OLD" });

		OperationScript script{ .decisions = { act(DecisionAction::Merge), act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed); // Skips do not demote
		CHECK(summary.skippedItems == 1);
		CHECK(summary.completedItems == 2); // The merged root directory and free.bin
		CHECK(readFileContents(base % "/dest/src/colliding.bin") == QByteArray{ "OLD" });
		CHECK(readFileContents(base % "/dest/src/free.bin") == patternedContents(200));
	}

	SECTION("skipping a directory root before it is ever scanned")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/deep/deeper"));
		writeTestFile(base % "/src/deep/deeper/x.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/dest/src"));

		OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1); // Only the unscanned root is known
		CHECK(summary.completedItems == 0);
		for (const ProgressSnapshot& snapshot : script.progress)
			CHECK(snapshot.phase != OperationPhase::Scanning); // Root-first resolution: no scan happened
	}
}

TEST_CASE("copy executor: same-object roots are silently already satisfied", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(500);
	writeTestFile(base % "/a.bin", contents);
	REQUIRE(createHardLink(base % "/a.bin", base % "/alias.bin"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/alias.bin");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.alreadySatisfiedItems == 1);
	CHECK(summary.completedItems == 0);
	CHECK(script.seenRequests.empty());
	CHECK(readFileContents(base % "/alias.bin") == contents);
}

TEST_CASE("outcome aggregation follows the exact precedence", "[executor]")
{
	using enum NodeOutcome;

	// Cancelled wins over everything, Failed over the rest, skipped/partial children make Partial
	CHECK(aggregateChildOutcome(Completed, Cancelled) == Cancelled);
	CHECK(aggregateChildOutcome(Failed, Cancelled) == Cancelled);
	CHECK(aggregateChildOutcome(Cancelled, Completed) == Cancelled);
	CHECK(aggregateChildOutcome(Completed, Failed) == Failed);
	CHECK(aggregateChildOutcome(Partial, Failed) == Failed);
	CHECK(aggregateChildOutcome(Failed, Skipped) == Failed); // Failure is never diluted to Partial
	CHECK(aggregateChildOutcome(Completed, Skipped) == Partial);
	CHECK(aggregateChildOutcome(Completed, Partial) == Partial);
	CHECK(aggregateChildOutcome(Partial, Completed) == Partial); // Partial is sticky
	CHECK(aggregateChildOutcome(Completed, AlreadySatisfied) == Completed); // Satisfied work prevents nothing
	CHECK(aggregateChildOutcome(Completed, Completed) == Completed);
}

TEST_CASE("copy executor: counters count only at the originating node", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// A deep chain with a skip at the very bottom: ancestors must not multiply the count.
	REQUIRE(QDir{}.mkpath(base % "/src/a/b/c"));
	writeTestFile(base % "/src/a/b/c/colliding.bin", patternedContents(100));
	REQUIRE(QDir{}.mkpath(base % "/dest/src/a/b/c"));
	writeTestFile(base % "/dest/src/a/b/c/colliding.bin", QByteArray{ "OLD" });

	OperationScript script{ .decisions = { act(DecisionAction::Merge), act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1); // The file, once - not once per ancestor
	CHECK(summary.completedItems == 4); // src (merged), a, b, c (merged silently as descendants)
	CHECK(summary.failedItems == 0);
}

TEST_CASE("copy executor: remembered decisions span items with accurate wording", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/one.bin", patternedContents(100));
	writeTestFile(base % "/src/two.bin", patternedContents(200));

	SECTION("Replace All answers the second collision silently")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest/src"));
		writeTestFile(base % "/dest/src/one.bin", QByteArray{ "OLD1" });
		writeTestFile(base % "/dest/src/two.bin", QByteArray{ "OLD2" });

		OperationScript script{ .decisions = { act(DecisionAction::Merge), act(DecisionAction::Replace, DecisionScope::RemainingMatchingIssues) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 3);
		CHECK(script.seenRequests.size() == 2); // The merge and one replacement prompt
		CHECK(readFileContents(base % "/dest/src/one.bin") == patternedContents(100));
		CHECK(readFileContents(base % "/dest/src/two.bin") == patternedContents(200));
	}

	SECTION("one ActionFailed Skip All spans different failed actions")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::StagedCopy_CreateStaging_Native, accessDeniedCode); // Fails whichever file goes first
		hooks.forceNativeError(Point::StagedCopy_WriteStaging_Native, ioFailureCode);     // Fails the other one

		OperationScript script{ .decisions = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) } };
		const auto summary = runCopy(script, { base % "/src/one.bin", base % "/src/two.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 2);
		REQUIRE(script.seenRequests.size() == 1); // The second failure was answered by the remembered Skip
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->action == FailedAction::PrepareStagingFile); // Names the actual stage
	}
}

TEST_CASE("copy executor: Merge All answers the remaining root collisions", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// Two selected roots, both colliding: descendants merge silently, so only roots can ask twice.
	REQUIRE(QDir{}.mkpath(base % "/srcA"));
	writeTestFile(base % "/srcA/a.bin", patternedContents(100));
	REQUIRE(QDir{}.mkpath(base % "/srcB"));
	writeTestFile(base % "/srcB/b.bin", patternedContents(200));
	REQUIRE(QDir{}.mkpath(base % "/dest/srcA"));
	REQUIRE(QDir{}.mkpath(base % "/dest/srcB"));
	writeTestFile(base % "/dest/srcA/keep.bin", QByteArray{ "KEEP" });

	OperationScript script{ .decisions = { act(DecisionAction::Merge, DecisionScope::RemainingMatchingIssues) } };
	const auto summary = runCopy(script, { base % "/srcA", base % "/srcB" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 4); // Both merged roots and both files
	REQUIRE(script.seenRequests.size() == 1); // The second root's collision was answered from the remembered Merge
	CHECK(script.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);
	CHECK(readFileContents(base % "/dest/srcA/a.bin") == patternedContents(100));
	CHECK(readFileContents(base % "/dest/srcA/keep.bin") == QByteArray{ "KEEP" }); // Merged, not replaced
	CHECK(readFileContents(base % "/dest/srcB/b.bin") == patternedContents(200));
}

TEST_CASE("copy executor: retries restart a fresh staging session", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(1000);
	writeTestFile(base % "/a.bin", contents);

	SECTION("retry after a staging failure succeeds cleanly")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::StagedCopy_CreateStaging_Native, accessDeniedCode);

		OperationScript script{ .decisions = { act(DecisionAction::Retry) } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(readFileContents(base % "/copied.bin") == contents);
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 2); // A genuinely new session
	}

	SECTION("retry after a later-chunk disk-full failure discards partial staging and progress")
	{
		constexpr uint64_t chunkSize = 256;

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::StagedCopy_WriteStaging_Native, diskFullCode, 1, 1);

		bool observedCleanupBeforeDecision = false;
		OperationScript script{ .decisions = { act(DecisionAction::Retry) } };
		script.onDecisionRequest = [&](const DecisionRequest& request) {
			CHECK(request.issue.kind == IssueKind::ActionFailed);
			REQUIRE(request.issue.failure.has_value());
			CHECK(request.issue.failure->action == FailedAction::WriteDestination);
			CHECK(request.issue.failure->filesystemError.category == FileErrorCategory::NotEnoughSpace);
			CHECK(entryAbsent(base % "/copied.bin"));
			CHECK(stagingFileCount(base) == 0);
			observedCleanupBeforeDecision = true;
		};

		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin", chunkSize);

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(readFileContents(base % "/copied.bin") == contents);
		CHECK(stagingFileCount(base) == 0);
		CHECK(observedCleanupBeforeDecision);
		CHECK(script.seenRequests.size() == 1);
		CHECK(hooks.forcedErrorConsumed(Point::StagedCopy_WriteStaging_Native));
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 2);

		uint64_t discardedAttemptBytes = 0;
		bool sawRetryFromZero = false;
		for (const ProgressSnapshot& snapshot : script.progress)
		{
			CHECK(snapshot.bytesProcessed <= static_cast<uint64_t>(contents.size()));
			if (snapshot.phase != OperationPhase::Working || !snapshot.currentEntry.has_value())
				continue;
			if (!sawRetryFromZero && snapshot.currentEntryBytesProcessed > discardedAttemptBytes)
				discardedAttemptBytes = snapshot.currentEntryBytesProcessed;
			else if (discardedAttemptBytes > 0 && snapshot.currentEntryBytesProcessed == 0)
				sawRetryFromZero = true;
		}
		CHECK(discardedAttemptBytes > 0);
		CHECK(sawRetryFromZero);
		CHECK(summary.transferredBytes == static_cast<uint64_t>(contents.size()) + discardedAttemptBytes);
	}

	SECTION("replacement authorization does not answer a later publication failure")
	{
		writeTestFile(base % "/taken.bin", QByteArray{ "OLD" });

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, accessDeniedCode);

		OperationScript script{ .decisions = { act(DecisionAction::Replace), act(DecisionAction::Retry) } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/taken.bin");

		CHECK(summary.status == CompletionStatus::Completed);
		REQUIRE(script.seenRequests.size() == 2);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
		CHECK(script.seenRequests[1].issue.kind == IssueKind::ActionFailed); // Not another replacement question
		CHECK(script.seenRequests[1].issue.failure->action == FailedAction::PublishDestination);
		CHECK(readFileContents(base % "/taken.bin") == contents);
	}

	SECTION("a phantom AlreadyExists at publication without a real collision is an ordinary failure")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, existsCode);

		OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed); // No re-resolution without a proven collision
		CHECK(stagingFileCount(base) == 0);
	}
}

TEST_CASE("copy executor: a staging preparation failure names the stage that failed", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/a.bin", patternedContents(4000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	FailedAction expectedAction = FailedAction::PreserveFileMetadata;
	FileErrorCategory expectedCategory = FileErrorCategory::PermissionDenied;
	SECTION("source metadata capture")
	{
		hooks.forceNativeError(Point::StagedCopy_CaptureMetadata_Native, accessDeniedCode);
	}
	SECTION("preallocation exhausting the volume")
	{
		hooks.forceNativeError(Point::StagedCopy_PreallocateStaging_Native, diskFullCode);
		expectedAction = FailedAction::PrepareStagingFile;
		expectedCategory = FileErrorCategory::NotEnoughSpace;
	}

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1);
	CHECK(summary.warningCount == 0); // Both exits clean up their own staging, if any exists yet
	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	REQUIRE(script.seenRequests[0].issue.failure.has_value());
	CHECK(script.seenRequests[0].issue.failure->action == expectedAction);
	CHECK(script.seenRequests[0].issue.failure->filesystemError.category == expectedCategory);
	CHECK(entryAbsent(base % "/dest/a.bin"));
	CHECK(stagingFileCount(base % "/dest") == 0);
}

TEST_CASE("copy executor: a commit-stage failure leaves the old destination in place", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray oldContents{ "OLD DESTINATION" };
	writeTestFile(base % "/a.bin", patternedContents(4000));
	writeTestFile(base % "/dest.bin", oldContents);

	CFaultHookScope hooks;
	FailedAction expectedAction = FailedAction::WriteDestination;
	SECTION("flush") { hooks.forceNativeError(Point::StagedCopy_FlushStaging_Native, ioFailureCode); }
	SECTION("metadata application")
	{
		hooks.forceNativeError(Point::StagedCopy_ApplyMetadata_Native, accessDeniedCode);
		expectedAction = FailedAction::PreserveFileMetadata;
	}
	SECTION("pre-publication close") { hooks.forceNativeError(Point::StagedCopy_CloseStaging_Native, ioFailureCode); }

	OperationScript script{ .decisions = { act(DecisionAction::Replace), act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1);
	REQUIRE(script.seenRequests.size() == 2);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
	CHECK(script.seenRequests[1].issue.kind == IssueKind::ActionFailed); // The authorization does not answer the failure
	REQUIRE(script.seenRequests[1].issue.failure.has_value());
	CHECK(script.seenRequests[1].issue.failure->action == expectedAction);
	CHECK(readFileContents(base % "/dest.bin") == oldContents);
	CHECK(stagingFileCount(base) == 0);
	// An authorized replacement is one of the two cases the durability contract flushes for.
	CHECK(hooks.arrivalCount(Point::StagedCopy_FlushStaging_Native) == 1);
}

TEST_CASE("copy executor: a retry after a commit-stage failure leaves no staging behind", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(4000);
	writeTestFile(base % "/a.bin", contents);
	writeTestFile(base % "/dest.bin", QByteArray{ "OLD" });

	// The failure point is past the whole transfer, so the abandoned attempt holds a fully staged file.
	CFaultHookScope hooks;
	hooks.forceNativeError(Point::StagedCopy_CloseStaging_Native, ioFailureCode);

	OperationScript script{ .decisions = { act(DecisionAction::Replace), act(DecisionAction::Retry) } };
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(readFileContents(base % "/dest.bin") == contents);
	CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 2); // A genuinely new session
	CHECK(stagingFileCount(base) == 0);
	CHECK(script.seenRequests.size() == 2); // The retry did not re-ask for replacement authorization
}

#ifdef _WIN32
TEST_CASE("copy executor: authorized replacement publishes over a read-only destination without restaging", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	const QByteArray contents = patternedContents(1000);
	writeTestFile(base % "/source.bin", contents);
	writeTestFile(base % "/destination.bin", QByteArray{ "OLD" });
	setFileReadOnly(base % "/destination.bin", true);

	CFaultHookScope hooks;
	OperationScript script{ .decisions = { act(DecisionAction::Replace) } };
	const auto summary = runCopy(script, { base % "/source.bin" }, DestinationIntent::ExactEntry, base % "/destination.bin");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
	CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 1);
	CHECK(hooks.arrivalCount(Point::RenameEntry_Native) == 2);
	CHECK(readFileContents(base % "/source.bin") == contents);
	CHECK(readFileContents(base % "/destination.bin") == contents);
}
#endif

TEST_CASE("copy executor: a retried publication does not push progress past the file size", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(3000);
	writeTestFile(base % "/a.bin", contents);

	// The first publication fails after every chunk has been staged and counted; the abandoned attempt must
	// un-count its bytes so the retry's progress starts from zero rather than doubling.
	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RenameEntry_Native, ioFailureCode);

	OperationScript script{ .decisions = { act(DecisionAction::Retry) } };
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin", 512);

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(readFileContents(base % "/copied.bin") == contents);
	for (const ProgressSnapshot& snapshot : script.progress)
		CHECK(snapshot.bytesProcessed <= 3000); // No snapshot overshoots the single file's size
}

TEST_CASE("copy executor: a file colliding with an existing directory is a type mismatch", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeTestFile(base % "/a.bin", patternedContents(120));
	REQUIRE(QDir{}.mkpath(base % "/dest/a.bin")); // A directory occupies the incoming file's target name

	SECTION("skip leaves the directory intact")
	{
		OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::TypeMismatch);
		CHECK(!entryAbsent(base % "/dest/a.bin"));
	}

	SECTION("rename to a free name completes the copy")
	{
		OperationScript script{ .decisions = { Decision{ DecisionAction::Rename, DecisionScope::ThisItem, QStringLiteral("a_copy.bin") } } };
		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(readFileContents(base % "/dest/a_copy.bin") == patternedContents(120));
	}
}

TEST_CASE("copy executor: a collision appearing at publication re-enters resolution", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	const QByteArray contents = patternedContents(2000);
	writeTestFile(base % "/a.bin", contents);

	// The destination is absent at resolution but a racing writer creates it mid-transfer (once staging exists,
	// before publication). The executor must detect the proven fresh collision, re-resolve, and publish over it.
	OperationScript script{ .decisions = { act(DecisionAction::Replace) } };
	script.onCheckpoint = [&] {
		if (stagingFileCount(base) > 0 && entryAbsent(base % "/copied.bin"))
			writeTestFile(base % "/copied.bin", QByteArray{ "RACE" });
	};

	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/copied.bin", 256);

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	REQUIRE(script.seenRequests.size() == 1); // No ActionFailed: the fresh collision re-resolves silently
	CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
	CHECK(readFileContents(base % "/copied.bin") == contents); // Published over the raced file
}

TEST_CASE("copy executor: a source resized mid-transfer is copied at its captured size", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QByteArray contents = patternedContents(10'000);
	writeTestFile(base % "/a.bin", contents);
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	// The size the copy works to is captured at the start of the file, so the mutation has to land after
	// the transfer is underway: the checkpoint between two chunks is that point, and no source mapping is
	// live there.
	bool sourceResized = false;
	OperationScript script;

	SECTION("growth past the captured size is not transferred")
	{
		script.onCheckpoint = [&] {
			if (sourceResized || stagingFileCount(base % "/dest") == 0)
				return;
			QFile source{ base % "/a.bin" };
			REQUIRE(source.open(QFile::Append));
			REQUIRE(source.write(QByteArray(5000, 'X')) == 5000);
			sourceResized = true;
		};

		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest", 256);

		CHECK(sourceResized);
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(summary.transferredBytes == 10'000);
		CHECK(script.seenRequests.empty());
		CHECK(readFileContents(base % "/dest/a.bin") == contents); // The appended tail is outside this copy
	}

	SECTION("truncation below the captured size fails the chunk that falls off the end")
	{
		script.decisions = { act(DecisionAction::Skip) };
		script.onCheckpoint = [&] {
			if (sourceResized || stagingFileCount(base % "/dest") == 0)
				return;
			REQUIRE(QFile{ base % "/a.bin" }.resize(2000));
			sourceResized = true;
		};

		const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest", 256);

		CHECK(sourceResized);
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->action == FailedAction::ReadSource);
		CHECK(entryAbsent(base % "/dest/a.bin")); // Nothing partial is ever published
		CHECK(stagingFileCount(base % "/dest") == 0);
	}
}

#ifdef _WIN32
TEST_CASE("copy executor: a directory raced into a Replace destination re-enters resolution", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	const QByteArray contents = patternedContents(2000);
	writeTestFile(base % "/source.bin", contents);
	writeTestFile(base % "/destination.bin", QByteArray{ "OLD" });

	CFaultHookScope hooks;
	hooks.armBarrier(Point::RenameEntry_Native);

	OperationScript script{ .decisions = {
		act(DecisionAction::Replace),
		Decision{ DecisionAction::Rename, DecisionScope::ThisItem, QStringLiteral("renamed.bin") } } };
	const auto request = makeTransferRequest(TransferKind::Copy, { base % "/source.bin" },
		DestinationIntent::ExactEntry, base % "/destination.bin");
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, 256 };

	OperationSummary summary;
	std::thread worker{ [&] { summary = executor.run(*request); } };

	REQUIRE(hooks.waitForBarrier(Point::RenameEntry_Native, std::chrono::milliseconds{ 5000 }));
	REQUIRE(QFile::remove(base % "/destination.bin"));
	REQUIRE(QDir{}.mkpath(base % "/destination.bin"));
	hooks.releaseBarrier(Point::RenameEntry_Native);
	worker.join();

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	REQUIRE(script.seenRequests.size() == 2);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
	CHECK(script.seenRequests[1].issue.kind == IssueKind::TypeMismatch);
	REQUIRE(script.seenRequests[1].issue.destination.has_value());
	CHECK(script.seenRequests[1].issue.destination->kind == OperationEntryKind::Directory);
	CHECK(QFileInfo{ base % "/destination.bin" }.isDir());
	CHECK(readFileContents(base % "/renamed.bin") == contents);
}
#endif

TEST_CASE("copy executor: links materialize", "[executor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	if (symlinkCreationUnavailable(base))
		return;

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/real.bin", patternedContents(400));
	writeTestFile(base % "/linktarget.bin", patternedContents(800));
	REQUIRE(createFileSymlink(base % "/linktarget.bin", base % "/src/filelink.bin"));
	REQUIRE(QDir{}.mkpath(base % "/dirtarget"));
	writeTestFile(base % "/dirtarget/inside.bin", patternedContents(900));
	REQUIRE(createDirectoryLink(base % "/dirtarget", base % "/src/dirlink"));
#ifdef _WIN32
	// A listed child is classified from the reparse tag the directory listing carries, not from a separate
	// inspection, and the junction above cannot reach the symlink tag through that path.
	REQUIRE(createDirectorySymlink(base % "/dirtarget", base % "/src/dirsymlink"));
#endif
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(script.seenRequests.empty());

	CHECK(readFileContents(base % "/dest/src/real.bin") == patternedContents(400));
	CHECK(readFileContents(base % "/dest/src/filelink.bin") == patternedContents(800)); // Materialized target content
	CHECK(!QFileInfo{ base % "/dest/src/filelink.bin" }.isSymLink());
	CHECK(readFileContents(base % "/dest/src/dirlink/inside.bin") == patternedContents(900)); // Materialized directory
	CHECK(!QFileInfo{ base % "/dest/src/dirlink" }.isSymLink());
#ifdef _WIN32
	CHECK(readFileContents(base % "/dest/src/dirsymlink/inside.bin") == patternedContents(900));
	CHECK(!QFileInfo{ base % "/dest/src/dirsymlink" }.isSymLink());
#endif
}

#ifndef _WIN32
TEST_CASE("copy executor: an Other entry is skippable", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/real.bin", patternedContents(400));
	REQUIRE(::mkfifo(QFile::encodeName(base % "/src/pipe").constData(), 0644) == 0);
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1);
	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::UnsupportedEntry);

	CHECK(readFileContents(base % "/dest/src/real.bin") == patternedContents(400));
	CHECK(!QFileInfo{ base % "/dest/src/pipe" }.exists());
}
#endif

TEST_CASE("copy executor: a cycle-terminated directory link materializes as an empty directory", "[executor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeTestFile(base % "/src/a.bin", patternedContents(100));
	REQUIRE(createDirectoryLink(base % "/src", base % "/src/sub/uplink"));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(readFileContents(base % "/dest/src/a.bin") == patternedContents(100));
	CHECK(QFileInfo{ base % "/dest/src/sub/uplink" }.isDir());
	CHECK(QDir{ base % "/dest/src/sub/uplink" }.isEmpty()); // The accepted cycle-termination edge
}

TEST_CASE("copy executor: directory timestamps are preserved for created directories only", "[executor][metadata]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeTestFile(base % "/src/sub/x.bin", patternedContents(100));
	REQUIRE(setEntryTimes(base % "/src/sub", { .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = 1'510'000'000 } }));
	REQUIRE(setEntryTimes(base % "/src", { .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = 1'500'000'000 } }));

	SECTION("created directories are stamped post-order")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		OperationScript script;
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.warningCount == 0);
		CHECK(entryLastWriteSeconds(base % "/dest/src") == 1'500'000'000); // Survived the child mutations: post-order
		CHECK(entryLastWriteSeconds(base % "/dest/src/sub") == 1'510'000'000);
	}

	SECTION("a pre-existing merge directory is not stamped, its created descendants are")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest/src"));

		OperationScript script{ .decisions = { act(DecisionAction::Merge) } };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		// The merge target's mtime moves with the filesystem's own child-creation updates, but the
		// operation must not have stamped the source's time onto it.
		CHECK(entryLastWriteSeconds(base % "/dest/src") != 1'500'000'000);
		CHECK(entryLastWriteSeconds(base % "/dest/src/sub") == 1'510'000'000); // Created under the merge: stamped
	}

	SECTION("a timestamp application failure is a bounded warning, not a failure")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::ApplyDirectoryTimes_Native, accessDeniedCode);

		OperationScript script;
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.warningCount == 1);
		REQUIRE(summary.representativeWarnings.size() == 1);
		CHECK(summary.representativeWarnings[0].failure.action == FailedAction::PreserveDirectoryTimestamps);
		CHECK(summary.failedItems == 0);
		CHECK(readFileContents(base % "/dest/src/sub/x.bin") == patternedContents(100)); // Contents intact
	}
}

TEST_CASE("copy executor: warnings beyond the representative cap keep an exact count", "[executor][metadata]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// One timestamp warning per created directory; 17 exceeds the 16-representative retention cap.
	QStringList sources;
	for (int i = 1; i <= 17; ++i)
	{
		const QString dir = base % "/src" % QString::number(i);
		REQUIRE(QDir{}.mkpath(dir));
		sources.push_back(dir);
	}
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	hooks.forceNativeError(Point::ApplyDirectoryTimes_Native, accessDeniedCode, 17);

	OperationScript script;
	const auto summary = runCopy(script, sources, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 17);
	CHECK(summary.failedItems == 0);
	CHECK(summary.warningCount == 17);
	REQUIRE(summary.representativeWarnings.size() == 16);
	for (const auto& warning : summary.representativeWarnings)
		CHECK(warning.failure.action == FailedAction::PreserveDirectoryTimestamps);
	CHECK(script.seenRequests.empty()); // Warnings never await a decision
}

TEST_CASE("copy executor: a failed staging cleanup is a warning, not a failure", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/a.bin", patternedContents(500));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	FailedAction expectedPrimaryAction = FailedAction::WriteDestination;
	FileErrorCategory expectedPrimaryCategory = FileErrorCategory::IoFailure;
	SECTION("after staging begins")
	{
		hooks.forceNativeError(Point::StagedCopy_WriteStaging_Native, ioFailureCode);
	}
	SECTION("while staging begins")
	{
		hooks.forceNativeError(Point::StagedCopy_ResizeStaging_Native, accessDeniedCode);
		expectedPrimaryAction = FailedAction::PrepareStagingFile;
		expectedPrimaryCategory = FileErrorCategory::PermissionDenied;
	}
	// Not a PermissionDenied-class code: that one the cleanup remediates (make writable, retry) and succeeds.
	hooks.forceNativeError(Point::StagedCopy_RemoveStaging_Native, ioFailureCode);

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/a.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1);
	CHECK(summary.failedItems == 0);
	CHECK(summary.warningCount == 1);
	REQUIRE(summary.representativeWarnings.size() == 1);
	CHECK(summary.representativeWarnings[0].failure.action == FailedAction::CleanupStaging);

	REQUIRE(script.seenRequests.size() == 1); // Only the primary failure asked; the cleanup failure never does
	CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	REQUIRE(script.seenRequests[0].issue.failure.has_value());
	CHECK(script.seenRequests[0].issue.failure->action == expectedPrimaryAction);
	CHECK(script.seenRequests[0].issue.failure->filesystemError.category == expectedPrimaryCategory);
	CHECK(stagingFileCount(base % "/dest") == 1); // The staging file the failed cleanup left behind
}

TEST_CASE("copy executor: a missing source root prompts and skips", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/ghost.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1);
	CHECK(summary.completedItems == 0);

	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	CHECK(script.seenRequests[0].issue.source.kind == OperationEntryKind::Unknown);
	REQUIRE(script.seenRequests[0].issue.failure.has_value());
	CHECK(script.seenRequests[0].issue.failure->action == FailedAction::InspectSource);
	CHECK(script.seenRequests[0].issue.failure->filesystemError.category == FileErrorCategory::NotFound);
}

TEST_CASE("copy executor: skipping a scanned directory counts its whole subtree", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/A/C"));
	writeTestFile(base % "/src/A/b.bin", patternedContents(100));
	writeTestFile(base % "/src/A/C/d.bin", patternedContents(200));
	REQUIRE(QDir{}.mkpath(base % "/dest/src"));
	writeTestFile(base % "/dest/src/A", patternedContents(10)); // A file occupying the incoming directory's name

	OperationScript script{ .decisions = { act(DecisionAction::Merge), act(DecisionAction::Skip) } };
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1); // The merged root directory
	CHECK(summary.skippedItems == 4); // A, b.bin, C, d.bin - the whole scanned subtree, not one item

	REQUIRE(script.seenRequests.size() == 2);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);
	CHECK(script.seenRequests[1].issue.kind == IssueKind::TypeMismatch);
	CHECK(readFileContents(base % "/dest/src/A") == patternedContents(10)); // The occupying file is untouched
	CHECK(readFileContents(base % "/src/A/C/d.bin") == patternedContents(200));
}

TEST_CASE("copy executor: cancellation", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/one.bin", patternedContents(1000));
	writeTestFile(base % "/src/two.bin", patternedContents(1000));
	REQUIRE(setEntryTimes(base % "/src", { .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = 1'500'000'000 } }));

	SECTION("a checkpoint cancellation ends the job as Cancelled with no staging leftovers")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		OperationScript script;
		// Cancel at the first checkpoint at which a staged transfer is underway; the abort must leave
		// no staging leftovers behind.
		script.cancelAtCheckpoint = [&] { return stagingFileCount(base % "/dest/src") > 0; };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(stagingFileCount(base % "/dest") == 0);
		CHECK(stagingFileCount(base % "/dest/src") == 0);
	}

	SECTION("cancellation during the final write prevents publication")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.armBarrier(Point::StagedCopy_WriteStaging_Native);

		std::atomic_bool cancellationRequested = false;
		OperationScript script;
		script.cancelAtCheckpoint = [&] { return cancellationRequested.load(); };
		const auto request = makeTransferRequest(TransferKind::Copy, { base % "/src/one.bin" },
			DestinationIntent::ExactEntry, base % "/dest/one.bin");
		REQUIRE(request.has_value());
		auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
		CTransferExecutor executor{ context, defaultTransferChunkSize };

		OperationSummary summary;
		std::thread worker{ [&] { summary = executor.run(*request); } };

		REQUIRE(hooks.waitForBarrier(Point::StagedCopy_WriteStaging_Native, std::chrono::milliseconds{ 5000 }));
		cancellationRequested.store(true);
		hooks.releaseBarrier(Point::StagedCopy_WriteStaging_Native);
		worker.join();

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.completedItems == 0);
		CHECK(!entryAbsent(base % "/src/one.bin"));
		CHECK(entryAbsent(base % "/dest/one.bin"));
		CHECK(stagingFileCount(base % "/dest") == 0);
	}

	SECTION("cancellation during a child suppresses the created parent's timestamp finalization")
	{
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		// Cancel at the first checkpoint after the destination directory exists: its children are then
		// still unprocessed, and the created directory must not be stamped with the source time.
		OperationScript script;
		script.cancelAtCheckpoint = [&] { return QFileInfo{ base % "/dest/src" }.isDir(); };
		const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		REQUIRE(QFileInfo{ base % "/dest/src" }.isDir()); // The directory was created before the cancellation...
		CHECK(entryLastWriteSeconds(base % "/dest/src") != 1'500'000'000); // ...but was not stamped with the source time
	}
}

TEST_CASE("copy executor: progress across scanning and working", "[executor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/srcA"));
	writeTestFile(base % "/srcA/a.bin", patternedContents(200'000)); // Multi-chunk at the test chunk size
	REQUIRE(QDir{}.mkpath(base % "/srcB"));
	writeTestFile(base % "/srcB/b.bin", patternedContents(1000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/srcA", base % "/srcB" }, DestinationIntent::IntoDirectory, base % "/dest");
	REQUIRE(summary.status == CompletionStatus::Completed);

	// Scanning and working alternate; find the last scanning stretch (root B's).
	size_t lastScanningIndex = 0;
	bool sawScanning = false;
	for (size_t i = 0; i < script.progress.size(); ++i)
	{
		if (script.progress[i].phase == OperationPhase::Scanning)
		{
			lastScanningIndex = i;
			sawScanning = true;
		}
	}
	REQUIRE(sawScanning);
	REQUIRE(lastScanningIndex + 1 < script.progress.size());

	// Aggregate totals stay absent until every root's manifest is known, then become exact.
	for (size_t i = 0; i < lastScanningIndex; ++i)
		CHECK(!script.progress[i].bytesTotal.has_value());
	const ProgressSnapshot& finalSnapshot = script.progress.back();
	REQUIRE(finalSnapshot.bytesTotal.has_value());
	CHECK(*finalSnapshot.bytesTotal == 201'000);
	CHECK(finalSnapshot.bytesProcessed == 201'000);
	CHECK(finalSnapshot.phase == OperationPhase::Working);

	// The multi-chunk file advanced its per-entry counter through intermediate snapshots.
	bool sawPartialEntryProgress = false;
	for (const ProgressSnapshot& snapshot : script.progress)
	{
		if (snapshot.phase == OperationPhase::Working && snapshot.currentEntryBytesProcessed > 0
			&& snapshot.currentEntryBytesTotal && snapshot.currentEntryBytesProcessed < *snapshot.currentEntryBytesTotal)
		{
			sawPartialEntryProgress = true;
			break;
		}
	}
	CHECK(sawPartialEntryProgress);
}

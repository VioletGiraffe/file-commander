// WP5: the recursive copy executor - the composed behavior of resolver, tree builder, staged copy,
// context policy, outcome aggregation, accounting, timestamps, and progress.

#include "fileoperations/ctransferexecutor.h"
#include "fileoperations/coperationexecutioncontext.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h> // mkfifo
#endif

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode accessDeniedCode = ERROR_ACCESS_DENIED;
constexpr NativeErrorCode existsCode = ERROR_FILE_EXISTS;
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
#else
constexpr NativeErrorCode accessDeniedCode = EACCES;
constexpr NativeErrorCode existsCode = EEXIST;
constexpr NativeErrorCode ioFailureCode = EIO;
#endif

OperationSummary runCopy(OperationScript& script, const QStringList& sources, const DestinationIntent intent, const QString& destination,
	const uint64_t chunkSize = 64 * 1024)
{
	const auto request = makeTransferRequest(TransferKind::Copy, sources, intent, destination);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, chunkSize };
	return executor.run(*request);
}

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
	buildRandomTree(base % "/src", 3);
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == countTreeEntries(base % "/src") + 1); // Every entry plus the root itself
	CHECK(summary.skippedItems == 0);
	requireEqualTrees(base % "/src", base % "/dest/src");
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

#ifndef _WIN32
TEST_CASE("copy executor: Other entries are skippable, links materialize", "[executor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/real.bin", patternedContents(400));
	REQUIRE(::mkfifo(QFile::encodeName(base % "/src/pipe").constData(), 0644) == 0);
	writeTestFile(base % "/linktarget.bin", patternedContents(800));
	REQUIRE(QFile::link(base % "/linktarget.bin", base % "/src/filelink.bin"));
	REQUIRE(QDir{}.mkpath(base % "/dirtarget"));
	writeTestFile(base % "/dirtarget/inside.bin", patternedContents(900));
	REQUIRE(createDirectoryLink(base % "/dirtarget", base % "/src/dirlink"));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } }; // For the FIFO
	const auto summary = runCopy(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.skippedItems == 1); // The FIFO
	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::UnsupportedEntry);

	CHECK(readFileContents(base % "/dest/src/real.bin") == patternedContents(400));
	CHECK(readFileContents(base % "/dest/src/filelink.bin") == patternedContents(800)); // Materialized target content
	CHECK(!QFileInfo{ base % "/dest/src/filelink.bin" }.isSymLink());
	CHECK(readFileContents(base % "/dest/src/dirlink/inside.bin") == patternedContents(900)); // Materialized directory
	CHECK(!QFileInfo{ base % "/dest/src/dirlink" }.isSymLink());
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

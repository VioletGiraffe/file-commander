// WP6: the post-order permanent-delete executor - manifest-driven removal, the read-only
// preflight/reactive policy, parent preservation, accounting, and item-based progress.

#include "fileoperations/cdeleteexecutor.h"
#include "fileoperations/coperationexecutioncontext.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h> // mkfifo
#endif

#include <algorithm>
#include <chrono>
#include <thread>

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode accessDeniedCode = ERROR_ACCESS_DENIED;
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
constexpr NativeErrorCode readOnlyCode = ERROR_FILE_READ_ONLY; // Classified as the unambiguous ReadOnly category
#else
constexpr NativeErrorCode accessDeniedCode = EACCES;
constexpr NativeErrorCode ioFailureCode = EIO;
constexpr NativeErrorCode readOnlyCode = EROFS;
#endif

OperationSummary runDelete(OperationScript& script, const QStringList& sources)
{
	const auto request = makePermanentDeleteRequest(sources);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Items);
	CDeleteExecutor executor{ context };
	return executor.run(*request);
}

} // namespace

TEST_CASE("delete executor: basic and multi-root deletion", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a single file")
	{
		writeTestFile(base % "/f.bin", patternedContents(100));

		const auto summary = runDelete(script, { base % "/f.bin" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(summary.skippedItems == 0);
		CHECK(summary.failedItems == 0);
		CHECK(entryAbsent(base % "/f.bin"));
		CHECK(script.seenRequests.empty());
	}

	SECTION("an empty directory")
	{
		REQUIRE(QDir{}.mkpath(base % "/empty"));

		const auto summary = runDelete(script, { base % "/empty" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/empty"));
	}

	SECTION("a nested tree")
	{
		REQUIRE(QDir{}.mkpath(base % "/root/sub/deeper"));
		writeTestFile(base % "/root/a.bin", patternedContents(10));
		writeTestFile(base % "/root/sub/b.bin", patternedContents(20));
		writeTestFile(base % "/root/sub/deeper/c.bin", patternedContents(30));

		const auto summary = runDelete(script, { base % "/root" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 6); // root, sub, deeper, and the three files
		CHECK(entryAbsent(base % "/root"));
	}

	SECTION("multiple roots")
	{
		writeTestFile(base % "/single.bin", patternedContents(10));
		REQUIRE(QDir{}.mkpath(base % "/tree"));
		writeTestFile(base % "/tree/f.bin", patternedContents(20));

		const auto summary = runDelete(script, { base % "/single.bin", base % "/tree" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 3);
		CHECK(entryAbsent(base % "/single.bin"));
		CHECK(entryAbsent(base % "/tree"));
	}

	SECTION("a random tree")
	{
		srand(g_randomSeed);
		REQUIRE(QDir{}.mkpath(base % "/random"));
		buildRandomTree(base % "/random", 4);
		const size_t totalEntries = countTreeEntries(base % "/random") + 1; // Plus the root itself

		const auto summary = runDelete(script, { base % "/random" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == totalEntries);
		CHECK(summary.skippedItems == 0);
		CHECK(entryAbsent(base % "/random"));
	}
}

TEST_CASE("delete executor: absent and vanished entries", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("an absent root is already satisfied, not an error")
	{
		const auto summary = runDelete(script, { base % "/never-existed.bin" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 0);
		CHECK(summary.alreadySatisfiedItems == 1);
		CHECK(script.seenRequests.empty());
	}

	SECTION("a root vanishing between inspection and its scan is already satisfied")
	{
		REQUIRE(QDir{}.mkpath(base % "/root"));

		// Remove the root at the first checkpoint after scanning has begun (the builder publishes the
		// root's Scanning snapshot, then checkpoints before listing it) - after inspection, before the listing.
		script.onCheckpoint = [&] {
			if (!script.progress.empty() && QFileInfo::exists(base % "/root"))
				REQUIRE(QDir{ base % "/root" }.removeRecursively());
		};
		const auto summary = runDelete(script, { base % "/root" });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 0);
		CHECK(summary.alreadySatisfiedItems == 1);
		CHECK(script.seenRequests.empty());
	}

	SECTION("an entry vanishing between scan and removal is already satisfied")
	{
		const QString filePath = base % "/vanishing.bin";
		writeTestFile(filePath, patternedContents(50));

		CFaultHookScope hooks;
		hooks.armBarrier(Point::RemoveEntry_Native);

		// Validation and context setup stay on this thread; the worker runs only the executor,
		// so no Catch assertions can race between the two threads.
		const auto request = makePermanentDeleteRequest({ filePath });
		REQUIRE(request.has_value());
		auto context = makeScriptedContext(script, PrimaryProgressUnit::Items);
		CDeleteExecutor executor{ context };

		OperationSummary summary;
		std::thread worker{ [&] { summary = executor.run(*request); } };

		REQUIRE(hooks.waitForBarrier(Point::RemoveEntry_Native, std::chrono::milliseconds{ 5000 }));
		REQUIRE(QFile::remove(filePath));
		hooks.releaseBarrier(Point::RemoveEntry_Native);
		worker.join();

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 0);
		CHECK(summary.alreadySatisfiedItems == 1);
		CHECK(script.seenRequests.empty());
	}
}

TEST_CASE("delete executor: read-only preflight row behavior", "[deleteexecutor][readonly]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString filePath = base % "/readonly.bin";
	writeTestFile(filePath, patternedContents(100));
	setFileReadOnly(filePath, true);

	OperationScript script;

	SECTION("MakeWritable deletes the file")
	{
		script.decisions = { act(DecisionAction::MakeWritable) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(filePath));

		REQUIRE(script.seenRequests.size() == 1);
		const DecisionRequest& request = script.seenRequests.front();
		CHECK(request.issue.kind == IssueKind::ReadOnlySourceRemoval);
		CHECK(!request.issue.failure.has_value()); // Preflight, not a failed removal
		CHECK(request.remainingMatchingScopeAllowed);
		CHECK(request.allowedActions == AllowedActions{ DecisionAction::MakeWritable, DecisionAction::Retry, DecisionAction::Skip, DecisionAction::Cancel });
	}

	SECTION("Retry re-inspects: an external permission change is observed without remediation")
	{
		script.onDecisionRequest = [&](const DecisionRequest&) { setFileReadOnly(filePath, false); };
		script.decisions = { act(DecisionAction::Retry) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(filePath));
		CHECK(script.seenRequests.size() == 1); // The re-inspection found the file writable; no second question
	}

	SECTION("Skip preserves the file, read-only state untouched")
	{
		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 0);
		CHECK(summary.skippedItems == 1);
		CHECK(summary.failedItems == 0);
		CHECK(!QFileInfo{ filePath }.isWritable());
		CHECK(readFileContents(filePath) == patternedContents(100));

		setFileReadOnly(filePath, false); // Let QTemporaryDir clean up
	}

	SECTION("Cancel stops the operation, file preserved")
	{
		script.decisions = { act(DecisionAction::Cancel) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(!entryAbsent(filePath));

		setFileReadOnly(filePath, false);
	}

	SECTION("remembered MakeWritable applies to the remaining read-only files")
	{
		const QString secondPath = base % "/readonly2.bin";
		writeTestFile(secondPath, patternedContents(50));
		setFileReadOnly(secondPath, true);

		script.decisions = { act(DecisionAction::MakeWritable, DecisionScope::RemainingMatchingIssues) };
		const auto summary = runDelete(script, { filePath, secondPath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 2);
		CHECK(entryAbsent(filePath));
		CHECK(entryAbsent(secondPath));
		CHECK(script.seenRequests.size() == 1); // The second file consumed the remembered authorization
	}

	SECTION("remembered Skip prevents removal without becoming failure")
	{
		const QString secondPath = base % "/readonly2.bin";
		writeTestFile(secondPath, patternedContents(50));
		setFileReadOnly(secondPath, true);

		script.decisions = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) };
		const auto summary = runDelete(script, { filePath, secondPath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 0);
		CHECK(summary.skippedItems == 2);
		CHECK(summary.failedItems == 0);
		CHECK(!entryAbsent(filePath));
		CHECK(!entryAbsent(secondPath));
		CHECK(script.seenRequests.size() == 1);

		setFileReadOnly(filePath, false);
		setFileReadOnly(secondPath, false);
	}
}

TEST_CASE("delete executor: read-only question is isolated from remembered ActionFailed", "[deleteexecutor][readonly]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// Roots are processed in request order, so the forced failure deterministically hits the first root.
	const QString failingPath = base % "/failing.bin";
	const QString readOnlyPath = base % "/readonly.bin";
	writeTestFile(failingPath, patternedContents(10));
	writeTestFile(readOnlyPath, patternedContents(20));
	setFileReadOnly(readOnlyPath, true);

	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

	OperationScript script;
	script.decisions = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues), act(DecisionAction::MakeWritable) };
	const auto summary = runDelete(script, { failingPath, readOnlyPath });

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(summary.skippedItems == 1);
	CHECK(!entryAbsent(failingPath));
	CHECK(entryAbsent(readOnlyPath));

	// The remembered ActionFailed Skip must not answer the confirmed read-only question.
	REQUIRE(script.seenRequests.size() == 2);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	CHECK(script.seenRequests[1].issue.kind == IssueKind::ReadOnlySourceRemoval);
}

TEST_CASE("delete executor: remediation and removal failures", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a make-writable failure is ordinary ActionFailed")
	{
		if (readOnlySemanticsUnavailable())
			return;

		const QString filePath = base % "/readonly.bin";
		writeTestFile(filePath, patternedContents(100));
		setFileReadOnly(filePath, true);

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::SetEntryWritable_Native, accessDeniedCode);

		script.decisions = { act(DecisionAction::MakeWritable), act(DecisionAction::Skip) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		CHECK(!entryAbsent(filePath));
		CHECK(!QFileInfo{ filePath }.isWritable());

		REQUIRE(script.seenRequests.size() == 2);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ReadOnlySourceRemoval);
		CHECK(script.seenRequests[1].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[1].issue.failure.has_value());
		CHECK(script.seenRequests[1].issue.failure->action == FailedAction::MakeWritable);

		setFileReadOnly(filePath, false);
	}

	SECTION("an ordinary removal failure prompts ActionFailed; Retry succeeds")
	{
		const QString filePath = base % "/f.bin";
		writeTestFile(filePath, patternedContents(100));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

		script.decisions = { act(DecisionAction::Retry) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(filePath));

		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->action == FailedAction::RemoveEntry);
	}

	SECTION("generic access denied is never reclassified as read-only")
	{
		const QString filePath = base % "/f.bin";
		writeTestFile(filePath, patternedContents(100));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RemoveEntry_Native, accessDeniedCode);

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.skippedItems == 1);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->filesystemError.category == FileErrorCategory::PermissionDenied);
	}

	SECTION("an unconfirmed read-only removal result stays ActionFailed")
	{
		// The file is writable, so the reactive fresh inspection cannot confirm the forced ReadOnly result.
		const QString filePath = base % "/f.bin";
		writeTestFile(filePath, patternedContents(100));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RemoveEntry_Native, readOnlyCode);

		script.decisions = { act(DecisionAction::Retry) };
		const auto summary = runDelete(script, { filePath });

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(filePath));

		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		REQUIRE(script.seenRequests[0].issue.failure.has_value());
		CHECK(script.seenRequests[0].issue.failure->filesystemError.category == FileErrorCategory::ReadOnly);
	}

	SECTION("a read-only removal result on a directory is never remediated")
	{
		REQUIRE(QDir{}.mkpath(base % "/empty"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RemoveEntry_Native, readOnlyCode);

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runDelete(script, { base % "/empty" });

		CHECK(summary.skippedItems == 1);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	}
}

TEST_CASE("delete executor: raced read-only state is reclassified reactively", "[deleteexecutor][readonly]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString filePath = base % "/racing.bin";
	writeTestFile(filePath, patternedContents(100));

	// The file is writable at preflight; it becomes read-only while removal is held at the barrier, and
	// the forced ReadOnly result then stands for what the race would produce. The fresh reactive
	// inspection confirms it, so the read-only question is asked - with the removal failure attached.
	CFaultHookScope hooks;
	hooks.armBarrier(Point::RemoveEntry_Native);
	hooks.forceNativeError(Point::RemoveEntry_Native, readOnlyCode);

	OperationScript script;
	script.decisions = { act(DecisionAction::MakeWritable) };

	const auto request = makePermanentDeleteRequest({ filePath });
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Items);
	CDeleteExecutor executor{ context };

	OperationSummary summary;
	std::thread worker{ [&] { summary = executor.run(*request); } };

	REQUIRE(hooks.waitForBarrier(Point::RemoveEntry_Native, std::chrono::milliseconds{ 5000 }));
	setFileReadOnly(filePath, true);
	hooks.releaseBarrier(Point::RemoveEntry_Native);
	worker.join();

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(entryAbsent(filePath));

	REQUIRE(script.seenRequests.size() == 1);
	const DecisionRequest& seenRequest = script.seenRequests.front();
	CHECK(seenRequest.issue.kind == IssueKind::ReadOnlySourceRemoval);
	REQUIRE(seenRequest.issue.failure.has_value());
	CHECK(seenRequest.issue.failure->action == FailedAction::RemoveEntry);
}

TEST_CASE("delete executor: links and special entries are unlinked without target effects", "[deleteexecutor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a directory link inside a tree: the link goes, the target stays")
	{
		REQUIRE(QDir{}.mkpath(base % "/target"));
		writeTestFile(base % "/target/t.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/root/sub"));
		REQUIRE(createDirectoryLink(base % "/target", base % "/root/sub/dirlink"));

		const auto summary = runDelete(script, { base % "/root" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 3); // root, sub, and the link entry; never the borrowed content
		CHECK(entryAbsent(base % "/root"));
		CHECK(readFileContents(base % "/target/t.bin") == patternedContents(100));
		CHECK(script.seenRequests.empty());
	}

	SECTION("a directory link as the root")
	{
		REQUIRE(QDir{}.mkpath(base % "/target"));
		writeTestFile(base % "/target/t.bin", patternedContents(100));
		REQUIRE(createDirectoryLink(base % "/target", base % "/dirlink"));

		const auto summary = runDelete(script, { base % "/dirlink" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/dirlink"));
		CHECK(readFileContents(base % "/target/t.bin") == patternedContents(100));
	}

	SECTION("a broken directory link")
	{
		REQUIRE(QDir{}.mkpath(base % "/target"));
		REQUIRE(createDirectoryLink(base % "/target", base % "/brokenlink"));
		REQUIRE(QDir{}.rmdir(base % "/target"));

		const auto summary = runDelete(script, { base % "/brokenlink" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/brokenlink"));
		CHECK(script.seenRequests.empty());
	}

#ifndef _WIN32
	SECTION("a file symlink to a read-only target: no preflight, target untouched")
	{
		if (readOnlySemanticsUnavailable())
			return;

		writeTestFile(base % "/target.bin", patternedContents(100));
		setFileReadOnly(base % "/target.bin", true);
		REQUIRE(QFile::link(base % "/target.bin", base % "/link.bin"));

		const auto summary = runDelete(script, { base % "/link.bin" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/link.bin"));
		CHECK(!QFileInfo{ base % "/target.bin" }.isWritable()); // Still there, still read-only
		CHECK(script.seenRequests.empty());

		setFileReadOnly(base % "/target.bin", false);
	}

	SECTION("a broken file symlink")
	{
		REQUIRE(QFile::link(base % "/no-such-target.bin", base % "/broken.bin"));

		const auto summary = runDelete(script, { base % "/broken.bin" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/broken.bin"));
	}

	SECTION("a FIFO is unlinked without a prompt")
	{
		REQUIRE(QDir{}.mkpath(base % "/root"));
		REQUIRE(::mkfifo(QFile::encodeName(base % "/root/fifo").constData(), 0600) == 0);

		const auto summary = runDelete(script, { base % "/root" });
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 2);
		CHECK(entryAbsent(base % "/root"));
		CHECK(script.seenRequests.empty()); // Delete has no UnsupportedEntry question: Other entries are simply unlinked
	}
#endif
}

TEST_CASE("delete executor: parent preservation after skipped content", "[deleteexecutor]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// The read-only file targets the skip deterministically regardless of listing order.
	REQUIRE(QDir{}.mkpath(base % "/root/sub"));
	writeTestFile(base % "/root/sub/kept.bin", patternedContents(10));
	setFileReadOnly(base % "/root/sub/kept.bin", true);
	writeTestFile(base % "/root/removed.bin", patternedContents(20));

	OperationScript script;
	script.decisions = { act(DecisionAction::Skip) };
	const auto summary = runDelete(script, { base % "/root" });

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1); // Only removed.bin; the preserved directories are not counted anywhere
	CHECK(summary.skippedItems == 1);
	CHECK(summary.failedItems == 0);
	CHECK(entryAbsent(base % "/root/removed.bin"));
	CHECK(!entryAbsent(base % "/root/sub/kept.bin"));
	CHECK(!entryAbsent(base % "/root/sub"));
	CHECK(!entryAbsent(base % "/root"));

	setFileReadOnly(base % "/root/sub/kept.bin", false);
}

TEST_CASE("delete executor: depth-3 partial propagation preserves every ancestor", "[deleteexecutor]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root/mid/leaf"));
	writeTestFile(base % "/root/mid/leaf/file.bin", patternedContents(10));
	setFileReadOnly(base % "/root/mid/leaf/file.bin", true);
	writeTestFile(base % "/root/mid/sibling.bin", patternedContents(20));

	OperationScript script{ .decisions = { act(DecisionAction::Skip) } };
	const auto summary = runDelete(script, { base % "/root" });

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1); // Only sibling.bin
	CHECK(summary.skippedItems == 1);
	CHECK(summary.failedItems == 0);

	// The skip makes leaf Partial, and Partial must climb the whole chain: every ancestor is retained.
	CHECK(entryAbsent(base % "/root/mid/sibling.bin"));
	CHECK(!entryAbsent(base % "/root/mid/leaf/file.bin"));
	CHECK(!entryAbsent(base % "/root/mid/leaf"));
	CHECK(!entryAbsent(base % "/root/mid"));
	CHECK(!entryAbsent(base % "/root"));

	setFileReadOnly(base % "/root/mid/leaf/file.bin", false);
}

#ifndef _WIN32
TEST_CASE("delete executor: a read-only failure on a file link stays ActionFailed", "[deleteexecutor][link][readonly]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/target.bin", patternedContents(60));
	REQUIRE(QFile::link(base % "/target.bin", base % "/thelink"));

	// A ReadOnly-classified removal failure is only reinterpreted as the read-only policy question for a
	// regular file; a link entry is never remediated, so the failure stays a plain ActionFailed.
	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RemoveEntry_Native, readOnlyCode);

	OperationScript script{ .decisions = { act(DecisionAction::Retry) } };
	const auto summary = runDelete(script, { base % "/thelink" });

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);

	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
	CHECK(script.seenRequests[0].allowedActions == allowedActionsFor(IssueKind::ActionFailed));

	CHECK(entryAbsent(base % "/thelink"));
	CHECK(readFileContents(base % "/target.bin") == patternedContents(60)); // The target was never touched
}
#endif

TEST_CASE("delete executor: cancellation checkpoints", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root"));
	writeTestFile(base % "/root/f.bin", patternedContents(100));

	OperationScript script;

	SECTION("cancellation during scanning deletes nothing")
	{
		// The builder checkpoints before every listing, so once a Scanning snapshot has been seen,
		// cancellation still precedes any removal.
		script.cancelAtCheckpoint = [&script] {
			return std::any_of(script.progress.begin(), script.progress.end(),
				[](const ProgressSnapshot& snapshot) { return snapshot.phase == OperationPhase::Scanning; });
		};
		const auto summary = runDelete(script, { base % "/root" });

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.completedItems == 0);
		CHECK(!entryAbsent(base % "/root/f.bin"));
	}

	SECTION("cancellation before directory cleanup preserves the emptied directory")
	{
		// Cancel at the first checkpoint after the child's deletion is observable; by the
		// checkpoint-before-every-mutation contract, that is before the emptied directory's removal.
		script.cancelAtCheckpoint = [&] { return !QFileInfo::exists(base % "/root/f.bin"); };
		const auto summary = runDelete(script, { base % "/root" });

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/root/f.bin"));
		CHECK(!entryAbsent(base % "/root"));
	}
}

TEST_CASE("delete executor: item-based progress and summary", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/root/sub"));
	writeTestFile(base % "/root/a.bin", patternedContents(10));
	writeTestFile(base % "/root/sub/b.bin", patternedContents(20));

	OperationScript script;
	const auto summary = runDelete(script, { base % "/root" });
	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 4);

	REQUIRE(!script.progress.empty());

	bool sawScanning = false;
	for (const ProgressSnapshot& snapshot : script.progress)
	{
		if (snapshot.phase == OperationPhase::Scanning)
		{
			sawScanning = true;
			CHECK(!snapshot.itemsTotal.has_value()); // A partial aggregate must never look exact
		}
	}
	CHECK(sawScanning);

	const ProgressSnapshot& last = script.progress.back();
	CHECK(last.phase == OperationPhase::Working);
	REQUIRE(last.itemsTotal.has_value());
	CHECK(*last.itemsTotal == 4);
	CHECK(last.itemsProcessed == 4);
	REQUIRE(last.bytesTotal.has_value());
	CHECK(*last.bytesTotal == 0); // A delete moves no bytes
}

TEST_CASE("delete executor: multi-root totals stay absent until every root is scanned", "[deleteexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/rootA/sub"));
	writeTestFile(base % "/rootA/a.bin", patternedContents(10));
	writeTestFile(base % "/rootA/sub/b.bin", patternedContents(20));
	REQUIRE(QDir{}.mkpath(base % "/rootB"));
	writeTestFile(base % "/rootB/c.bin", patternedContents(30));
	writeTestFile(base % "/rootB/d.bin", patternedContents(40));
	writeTestFile(base % "/rootB/e.bin", patternedContents(50));

	OperationScript script;
	const auto summary = runDelete(script, { base % "/rootA", base % "/rootB" });

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 8);

	// One root's manifest alone must never publish as the exact total; once present, it is the full 8.
	REQUIRE(!script.progress.empty());
	CHECK(!script.progress.front().itemsTotal.has_value());
	for (const ProgressSnapshot& snapshot : script.progress)
	{
		if (snapshot.itemsTotal.has_value())
			CHECK(*snapshot.itemsTotal == 8);
	}
	REQUIRE(script.progress.back().itemsTotal.has_value());
}

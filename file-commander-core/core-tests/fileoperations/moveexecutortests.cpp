// WP7: the recursive move executor - rename-first boundaries, the cross-device staged-copy fallback,
// the pre-publication read-only policy, and the committed source-cleanup segment.

#include "fileoperations/ctransferexecutor.h"
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

#include <chrono>
#include <thread>

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode crossDeviceCode = ERROR_NOT_SAME_DEVICE;
constexpr NativeErrorCode accessDeniedCode = ERROR_ACCESS_DENIED;
constexpr NativeErrorCode ioFailureCode = ERROR_GEN_FAILURE;
constexpr NativeErrorCode readOnlyCode = ERROR_FILE_READ_ONLY;
#else
constexpr NativeErrorCode crossDeviceCode = EXDEV;
constexpr NativeErrorCode accessDeniedCode = EACCES;
constexpr NativeErrorCode ioFailureCode = EIO;
constexpr NativeErrorCode readOnlyCode = EROFS;
#endif

OperationSummary runMove(OperationScript& script, const QStringList& sources, const DestinationIntent intent, const QString& destination,
	const uint64_t chunkSize = 64 * 1024)
{
	const auto request = makeTransferRequest(TransferKind::Move, sources, intent, destination);
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context, chunkSize };
	return executor.run(*request);
}

} // namespace

TEST_CASE("move executor: same-filesystem renames", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a whole tree renames without a manifest scan")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/sub"));
		writeTestFile(base % "/src/a.bin", patternedContents(1000));
		writeTestFile(base % "/src/sub/b.bin", patternedContents(2000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1); // The root entry is the only known item: never scanned
		CHECK(summary.transferredBytes == 0);
		CHECK(entryAbsent(base % "/src"));
		CHECK(readFileContents(base % "/dest/src/a.bin") == patternedContents(1000));
		CHECK(readFileContents(base % "/dest/src/sub/b.bin") == patternedContents(2000));
		CHECK(script.seenRequests.empty());
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0); // Nothing was staged

		for (const ProgressSnapshot& snapshot : script.progress)
			CHECK(snapshot.phase == OperationPhase::Working); // No scanning ever happened
	}

	SECTION("an exact-entry file rename")
	{
		writeTestFile(base % "/a.bin", patternedContents(500));

		const auto summary = runMove(script, { base % "/a.bin" }, DestinationIntent::ExactEntry, base % "/moved.bin");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/a.bin"));
		CHECK(readFileContents(base % "/moved.bin") == patternedContents(500));
	}

	SECTION("a case-only respell is a real rename, not a silent same-object no-op")
	{
		writeTestFile(base % "/case.bin", patternedContents(100));

		const auto summary = runMove(script, { base % "/case.bin" }, DestinationIntent::ExactEntry, base % "/CASE.BIN");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(script.seenRequests.empty());
		CHECK(summary.alreadySatisfiedItems == 0);

		const QStringList names = QDir{ base }.entryList({ QStringLiteral("*.BIN"), QStringLiteral("*.bin") }, QDir::Files);
		CHECK(names.contains(QStringLiteral("CASE.BIN")));
		CHECK(!names.contains(QStringLiteral("case.bin")));
	}

	SECTION("same-filesystem file replacement is one atomic rename")
	{
		writeTestFile(base % "/src.bin", patternedContents(700));
		writeTestFile(base % "/dest.bin", patternedContents(50));

		CFaultHookScope hooks;
		script.decisions = { act(DecisionAction::Replace) };
		const auto summary = runMove(script, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/src.bin"));
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(700));
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0); // Renamed, not staged

		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::FileReplacement);
	}

	SECTION("moving onto a hardlink alias of the source")
	{
		writeTestFile(base % "/same.bin", patternedContents(100));
		REQUIRE(createHardLink(base % "/same.bin", base % "/alias.bin"));

		const auto summary = runMove(script, { base % "/same.bin" }, DestinationIntent::ExactEntry, base % "/alias.bin");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(script.seenRequests.empty());
#ifdef _WIN32
		// NTFS's exclusive rename exempts same-file destinations, so the rename-first attempt completes the
		// move natively: the source name is removed. Accepted divergence, see the design plan's same-object note.
		CHECK(summary.completedItems == 1);
		CHECK(summary.alreadySatisfiedItems == 0);
		CHECK(entryAbsent(base % "/same.bin"));
#else
		// POSIX exclusive rename refuses a same-inode destination, so resolution classifies the pair as the
		// same object: already satisfied, source retained.
		CHECK(summary.alreadySatisfiedItems == 1);
		CHECK(summary.completedItems == 0);
		CHECK(!entryAbsent(base % "/same.bin"));
#endif
		CHECK(readFileContents(base % "/alias.bin") == patternedContents(100));
	}

#ifndef _WIN32
	SECTION("an Other entry renames on the same filesystem")
	{
		REQUIRE(::mkfifo(QFile::encodeName(base % "/fifo").constData(), 0600) == 0);

		const auto summary = runMove(script, { base % "/fifo" }, DestinationIntent::ExactEntry, base % "/fifo-moved");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(base % "/fifo"));
		CHECK(!entryAbsent(base % "/fifo-moved"));
		CHECK(script.seenRequests.empty());
	}
#endif
}

TEST_CASE("move executor: a same-filesystem directory-link root renames as the link", "[moveexecutor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/target"));
	writeTestFile(base % "/target/x.bin", patternedContents(300));
	REQUIRE(createDirectoryLink(base % "/target", base % "/thelink"));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;
	CFaultHookScope hooks;
	const auto summary = runMove(script, { base % "/thelink" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(summary.transferredBytes == 0);
	CHECK(script.seenRequests.empty());
	CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0); // Renamed, never staged

	// The link itself moved: the destination is a link, the source name is gone, the target tree untouched.
	CHECK(entryAbsent(base % "/thelink"));
	const QFileInfo movedInfo{ base % "/dest/thelink" };
	CHECK((movedInfo.isSymLink() || movedInfo.isJunction()));
	CHECK(readFileContents(base % "/target/x.bin") == patternedContents(300));
	CHECK(readFileContents(base % "/dest/thelink/x.bin") == patternedContents(300)); // Still resolves through the link
}

TEST_CASE("move executor: cross-device fallback", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a single file is staged, published, and its source removed")
	{
		writeTestFile(base % "/f.bin", patternedContents(3000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/f.bin" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(summary.transferredBytes == 3000);
		CHECK(entryAbsent(base % "/f.bin"));
		CHECK(readFileContents(base % "/dest/f.bin") == patternedContents(3000));
		CHECK(script.seenRequests.empty());
	}

	SECTION("a tree is copied and the source removed post-order")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/sub/deeper"));
		writeTestFile(base % "/src/a.bin", patternedContents(1000));
		writeTestFile(base % "/src/sub/b.bin", patternedContents(2000));
		writeTestFile(base % "/src/sub/deeper/c.bin", patternedContents(3000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		// One forced CrossDevice at the root suffices: the whole subtree inherits the fallback and
		// attempts no further renames.
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 6); // Files and directories alike, each counted after its source cleanup
		CHECK(summary.transferredBytes == 6000);
		CHECK(entryAbsent(base % "/src"));
		CHECK(readFileContents(base % "/dest/src/a.bin") == patternedContents(1000));
		CHECK(readFileContents(base % "/dest/src/sub/b.bin") == patternedContents(2000));
		CHECK(readFileContents(base % "/dest/src/sub/deeper/c.bin") == patternedContents(3000));
		// The root attempt plus the three staged publications (which rename too) - and no per-child
		// rename attempts, which would add five more.
		CHECK(hooks.arrivalCount(Point::RenameEntry_Native) == 4);
	}

	SECTION("a mixed batch: one root falls back while the other renames in place")
	{
		writeTestFile(base % "/forced.bin", patternedContents(1000));
		REQUIRE(QDir{}.mkpath(base % "/renamed"));
		writeTestFile(base % "/renamed/r.bin", patternedContents(500));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode); // Consumed by the first root's attempt

		const auto summary = runMove(script, { base % "/forced.bin", base % "/renamed" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 2); // The copied file, and the renamed root as one unscanned item
		CHECK(summary.transferredBytes == 1000);
		CHECK(entryAbsent(base % "/forced.bin"));
		CHECK(entryAbsent(base % "/renamed"));
		CHECK(readFileContents(base % "/dest/forced.bin") == patternedContents(1000));
		CHECK(readFileContents(base % "/dest/renamed/r.bin") == patternedContents(500));
	}

	SECTION("alternating rename and scan phases, totals absent until the last manifest")
	{
		writeTestFile(base % "/r1.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/merged/inner"));
		writeTestFile(base % "/merged/m.bin", patternedContents(4000));
		writeTestFile(base % "/r3.bin", patternedContents(200));
		REQUIRE(QDir{}.mkpath(base % "/dest/merged")); // Occupied: the second root must merge, which requires its scan

		script.decisions = { act(DecisionAction::Merge) };
		const auto summary = runMove(script, { base % "/r1.bin", base % "/merged", base % "/r3.bin" },
			DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1 + 3 + 1); // r1; the merged root, inner, and m.bin; r3
		CHECK(entryAbsent(base % "/merged"));
		CHECK(readFileContents(base % "/dest/merged/m.bin") == patternedContents(4000));

		// Working (r1's rename), then Scanning (the merge root's manifest), then Working again.
		bool sawScanning = false, sawWorkingAfterScanning = false;
		for (const ProgressSnapshot& snapshot : script.progress)
		{
			if (snapshot.phase == OperationPhase::Scanning)
			{
				sawScanning = true;
				CHECK(!snapshot.bytesTotal.has_value());
				CHECK(!snapshot.itemsTotal.has_value());
			}
			else if (sawScanning)
				sawWorkingAfterScanning = true;
		}
		CHECK(script.progress.front().phase == OperationPhase::Working);
		CHECK(sawScanning);
		CHECK(sawWorkingAfterScanning);

		// Totals may appear only once every root's manifest requirement is known - here, at the very end.
		const ProgressSnapshot& last = script.progress.back();
		REQUIRE(last.itemsTotal.has_value());
		CHECK(*last.itemsTotal == 1 + 3 + 1);
	}
}

TEST_CASE("move executor: merge with per-child renames", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/shared"));
	REQUIRE(QDir{}.mkpath(base % "/src/unique"));
	writeTestFile(base % "/src/shared/fresh.bin", patternedContents(300));
	writeTestFile(base % "/src/unique/u.bin", patternedContents(400));
	writeTestFile(base % "/src/x.bin", patternedContents(500));
	writeTestFile(base % "/src/y.bin", patternedContents(600));

	REQUIRE(QDir{}.mkpath(base % "/dest/src/shared")); // Root and one descendant directory collide
	writeTestFile(base % "/dest/src/shared/keep.bin", patternedContents(10));
	writeTestFile(base % "/dest/src/x.bin", patternedContents(20)); // One file collides

	CFaultHookScope hooks;
	OperationScript script;
	script.decisions = { act(DecisionAction::Merge), act(DecisionAction::Replace) };
	const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(entryAbsent(base % "/src")); // Fully merged away, including the emptied source directories

	CHECK(readFileContents(base % "/dest/src/shared/fresh.bin") == patternedContents(300));
	CHECK(readFileContents(base % "/dest/src/shared/keep.bin") == patternedContents(10)); // Pre-existing content survives the merge
	CHECK(readFileContents(base % "/dest/src/unique/u.bin") == patternedContents(400));
	CHECK(readFileContents(base % "/dest/src/x.bin") == patternedContents(500)); // Replaced
	CHECK(readFileContents(base % "/dest/src/y.bin") == patternedContents(600));

	// Everything moved by rename: the descendant merge was silent, the non-colliding subtree and files
	// renamed directly, and the authorized replacement used the atomic rename form.
	CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0);
	REQUIRE(script.seenRequests.size() == 2);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);
	CHECK(script.seenRequests[1].issue.kind == IssueKind::FileReplacement);

	CHECK(summary.completedItems == 7); // src, shared, fresh.bin, unique subtree (2), x.bin, y.bin
}

TEST_CASE("move executor: links and borrowed content in the fallback", "[moveexecutor][link]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a directory link materializes its target; the link entry is removed, the target is not")
	{
		REQUIRE(QDir{}.mkpath(base % "/target"));
		writeTestFile(base % "/target/t.bin", patternedContents(800));
		REQUIRE(QDir{}.mkpath(base % "/src"));
		REQUIRE(createDirectoryLink(base % "/target", base % "/src/dirlink"));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(base % "/src")); // Including the link entry itself
		CHECK(readFileContents(base % "/target/t.bin") == patternedContents(800)); // Borrowed source content untouched
		CHECK(readFileContents(base % "/dest/src/dirlink/t.bin") == patternedContents(800)); // Materialized as a real directory
		CHECK(!QFileInfo{ base % "/dest/src/dirlink" }.isSymbolicLink());
		CHECK(script.seenRequests.empty());
	}

#ifndef _WIN32
	SECTION("a file symlink materializes its target content; the link entry is removed, the target is not")
	{
		writeTestFile(base % "/target.bin", patternedContents(900));
		REQUIRE(QDir{}.mkpath(base % "/src"));
		REQUIRE(QFile::link(base % "/target.bin", base % "/src/link.bin"));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(base % "/src"));
		CHECK(readFileContents(base % "/target.bin") == patternedContents(900));
		CHECK(readFileContents(base % "/dest/src/link.bin") == patternedContents(900));
		CHECK(!QFileInfo{ base % "/dest/src/link.bin" }.isSymbolicLink());
	}

	SECTION("a broken link cannot materialize: skipping it retains it and its ancestors")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/sub"));
		REQUIRE(QFile::link(base % "/src/sub/no-such-target", base % "/src/sub/broken"));
		writeTestFile(base % "/src/moved.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		CHECK(readFileContents(base % "/dest/src/moved.bin") == patternedContents(100));
		CHECK(entryAbsent(base % "/src/moved.bin"));
		CHECK(!entryAbsent(base % "/src/sub")); // The skipped link's ancestors are preserved
		CHECK(!entryAbsent(base % "/src"));
	}
#endif

	SECTION("a cycle-terminated directory link becomes an empty real directory")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/sub"));
		writeTestFile(base % "/src/f.bin", patternedContents(100));
		REQUIRE(createDirectoryLink(base % "/src", base % "/src/sub/uplink"));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(base % "/src"));
		CHECK(QFileInfo{ base % "/dest/src/sub/uplink" }.isDir());
		CHECK(QDir{ base % "/dest/src/sub/uplink" }.isEmpty());
		CHECK(readFileContents(base % "/dest/src/f.bin") == patternedContents(100));
	}

#ifndef _WIN32
	SECTION("an Other entry in the fallback is unsupported: skipping retains it and its ancestors")
	{
		REQUIRE(QDir{}.mkpath(base % "/src"));
		REQUIRE(::mkfifo(QFile::encodeName(base % "/src/fifo").constData(), 0600) == 0);
		writeTestFile(base % "/src/moved.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::UnsupportedEntry);
		CHECK(!entryAbsent(base % "/src/fifo"));
		CHECK(!entryAbsent(base % "/src"));
		CHECK(entryAbsent(base % "/src/moved.bin"));
	}
#endif
}

TEST_CASE("move executor: pre-publication read-only policy", "[moveexecutor][readonly]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString filePath = base % "/readonly.bin";
	writeTestFile(filePath, patternedContents(100));
	setFileReadOnly(filePath, true);
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	OperationScript script;

	SECTION("MakeWritable is asked before staging and applied only after publication")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.onDecisionRequest = [&](const DecisionRequest& request) {
			CHECK(request.issue.kind == IssueKind::ReadOnlySourceRemoval);
			CHECK(request.remainingMatchingScopeAllowed);
			CHECK(!request.issue.failure.has_value());
			CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0); // Nothing staged yet
			CHECK(!QFileInfo{ filePath }.isWritable()); // And the source is untouched
		};
		script.decisions = { act(DecisionAction::MakeWritable) };
		const auto summary = runMove(script, { filePath }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(filePath));
		CHECK(readFileContents(base % "/dest/readonly.bin") == patternedContents(100));
		CHECK(!QFileInfo{ base % "/dest/readonly.bin" }.isWritable()); // The destination keeps the source's read-only state
		CHECK(script.seenRequests.size() == 1);

		setFileReadOnly(base % "/dest/readonly.bin", false);
	}

	SECTION("a pre-publication failure leaves the source metadata unchanged despite the authorization")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::StagedCopy_CreateStaging_Native, accessDeniedCode);

		script.decisions = { act(DecisionAction::MakeWritable), act(DecisionAction::Skip) };
		const auto summary = runMove(script, { filePath }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		CHECK(!entryAbsent(filePath));
		CHECK(!QFileInfo{ filePath }.isWritable()); // The authorization was never applied
		CHECK(entryAbsent(base % "/dest/readonly.bin"));

		setFileReadOnly(filePath, false);
	}

	SECTION("Retry observes an external permission change")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.onDecisionRequest = [&](const DecisionRequest&) { setFileReadOnly(filePath, false); };
		script.decisions = { act(DecisionAction::Retry) };
		const auto summary = runMove(script, { filePath }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(entryAbsent(filePath));
		CHECK(script.seenRequests.size() == 1);
	}

	SECTION("a remembered Skip prevents publication of the remaining read-only files")
	{
		// One tree, so a single forced CrossDevice at the root routes both files to the copy-based path.
		REQUIRE(QDir{}.mkpath(base % "/src"));
		writeTestFile(base % "/src/ro1.bin", patternedContents(50));
		writeTestFile(base % "/src/ro2.bin", patternedContents(60));
		setFileReadOnly(base % "/src/ro1.bin", true);
		setFileReadOnly(base % "/src/ro2.bin", true);

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.decisions = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues) };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 2);
		CHECK(summary.failedItems == 0);
		CHECK(!entryAbsent(base % "/src/ro1.bin"));
		CHECK(!entryAbsent(base % "/src/ro2.bin"));
		CHECK(script.seenRequests.size() == 1);
		CHECK(hooks.arrivalCount(Point::StagedCopy_CreateStaging_Native) == 0); // Neither file was ever staged

		setFileReadOnly(base % "/src/ro1.bin", false);
		setFileReadOnly(base % "/src/ro2.bin", false);
		setFileReadOnly(filePath, false); // The case-level file, untouched by this section
	}

	SECTION("Cancel before publication preserves everything")
	{
		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.decisions = { act(DecisionAction::Cancel) };
		const auto summary = runMove(script, { filePath }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(!entryAbsent(filePath));
		CHECK(entryAbsent(base % "/dest/readonly.bin"));

		setFileReadOnly(filePath, false);
	}
}

TEST_CASE("move executor: committed cleanup", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	OperationScript script;

	SECTION("a removal failure retains both entries: Skip records one failed item and the job is Failed")
	{
		writeTestFile(base % "/f.bin", patternedContents(1000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runMove(script, { base % "/f.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Failed);
		CHECK(summary.completedItems == 0); // Moved counts only after required source removal
		CHECK(summary.failedItems == 1);
		REQUIRE(summary.representativeFailures.size() == 1);
		CHECK(summary.representativeFailures[0].failure.action == FailedAction::RemovePublishedMoveSource);
		CHECK(!entryAbsent(base % "/f.bin")); // Source retained...
		CHECK(readFileContents(base % "/dest/f.bin") == patternedContents(1000)); // ...and the destination is never rolled back

		REQUIRE(script.seenRequests.size() == 1);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		CHECK(!script.seenRequests[0].remainingMatchingScopeAllowed); // Item-only
	}

	SECTION("Retry completes the cleanup and the move")
	{
		writeTestFile(base % "/f.bin", patternedContents(1000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

		script.decisions = { act(DecisionAction::Retry) };
		const auto summary = runMove(script, { base % "/f.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.completedItems == 1);
		CHECK(summary.failedItems == 0);
		CHECK(entryAbsent(base % "/f.bin"));
	}

	SECTION("an earlier remembered pre-commit Skip All cannot answer a committed prompt")
	{
		REQUIRE(QDir{}.mkpath(base % "/src"));
		writeTestFile(base % "/src/a.bin", patternedContents(100));
		writeTestFile(base % "/src/b.bin", patternedContents(200));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		// Whichever child stages first fails pre-commit and is skipped with All scope; the other child
		// then hits a committed removal failure, which must still prompt.
		hooks.forceNativeError(Point::StagedCopy_CreateStaging_Native, accessDeniedCode);
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

		script.decisions = { act(DecisionAction::Skip, DecisionScope::RemainingMatchingIssues), act(DecisionAction::Retry) };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.skippedItems == 1);
		CHECK(summary.completedItems == 1); // The other file completed after the committed Retry
		CHECK(!entryAbsent(base % "/src")); // Partial: the skipped file and its parent remain

		REQUIRE(script.seenRequests.size() == 2);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ActionFailed);
		CHECK(script.seenRequests[0].remainingMatchingScopeAllowed);
		CHECK(script.seenRequests[1].issue.kind == IssueKind::ActionFailed);
		CHECK(!script.seenRequests[1].remainingMatchingScopeAllowed);
	}

	SECTION("a make-writable failure in the committed segment is item-only ActionFailed")
	{
		if (readOnlySemanticsUnavailable())
			return;

		writeTestFile(base % "/readonly.bin", patternedContents(100));
		setFileReadOnly(base % "/readonly.bin", true);
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::SetEntryWritable_Native, accessDeniedCode);

		script.decisions = { act(DecisionAction::MakeWritable), act(DecisionAction::Skip) };
		const auto summary = runMove(script, { base % "/readonly.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Failed);
		CHECK(summary.failedItems == 1);
		REQUIRE(summary.representativeFailures.size() == 1);
		CHECK(summary.representativeFailures[0].failure.action == FailedAction::MakeWritable);
		CHECK(!entryAbsent(base % "/readonly.bin"));
		CHECK(!QFileInfo{ base % "/readonly.bin" }.isWritable());
		CHECK(!entryAbsent(base % "/dest/readonly.bin"));

		REQUIRE(script.seenRequests.size() == 2);
		CHECK(script.seenRequests[0].issue.kind == IssueKind::ReadOnlySourceRemoval);
		CHECK(script.seenRequests[1].issue.kind == IssueKind::ActionFailed);
		CHECK(!script.seenRequests[1].remainingMatchingScopeAllowed);

		setFileReadOnly(base % "/readonly.bin", false);
		setFileReadOnly(base % "/dest/readonly.bin", false);
	}

	SECTION("cancellation overriding a committed prompt records the failure and ends Cancelled")
	{
		writeTestFile(base % "/f.bin", patternedContents(1000));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);

		script.cancelInsteadOfAnswering = true;
		const auto summary = runMove(script, { base % "/f.bin" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.failedItems == 1);
		REQUIRE(summary.representativeFailures.size() == 1);
		CHECK(summary.representativeFailures[0].failure.action == FailedAction::RemovePublishedMoveSource);
		CHECK(!entryAbsent(base % "/f.bin"));
		CHECK(!entryAbsent(base % "/dest/f.bin"));
	}

	SECTION("one committed-cleanup failure does not stop later roots; the job still ends Failed")
	{
		writeTestFile(base % "/failing.bin", patternedContents(100));
		writeTestFile(base % "/clean.bin", patternedContents(200));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode); // The first root falls back...
		hooks.forceNativeError(Point::RemoveEntry_Native, ioFailureCode);   // ...and its cleanup fails

		script.decisions = { act(DecisionAction::Skip) };
		const auto summary = runMove(script, { base % "/failing.bin", base % "/clean.bin" },
			DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Failed);
		CHECK(summary.failedItems == 1);
		CHECK(summary.completedItems == 1); // The second root renamed in place
		CHECK(!entryAbsent(base % "/failing.bin"));
		CHECK(entryAbsent(base % "/clean.bin"));
		CHECK(!entryAbsent(base % "/dest/clean.bin"));
	}
}

TEST_CASE("move executor: an already-satisfied child blocks owned-directory cleanup like a skip", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/a.bin", patternedContents(400));
	writeTestFile(base % "/src/b.bin", patternedContents(900));
	// The merge target already holds a hardlink alias of a.bin: that child's desired end state already holds.
	REQUIRE(QDir{}.mkpath(base % "/dest/src"));
	REQUIRE(createHardLink(base % "/src/a.bin", base % "/dest/src/a.bin"));

	CFaultHookScope hooks;
	// Forcing cross-device on the root rename keeps the children off the rename-first path, so the alias
	// child reaches resolution's same-object check uniformly on every platform.
	hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

	OperationScript script{ .decisions = { act(DecisionAction::Merge) } };
	const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.alreadySatisfiedItems == 1);
	CHECK(summary.completedItems == 1); // b.bin only; the source directory was retained, so it never counted
	CHECK(summary.skippedItems == 0);
	CHECK(summary.failedItems == 0);
	CHECK(summary.transferredBytes == 900);

	// The already-satisfied child keeps its source entry, which blocks the owned directory's cleanup.
	CHECK(!entryAbsent(base % "/src"));
	CHECK(readFileContents(base % "/src/a.bin") == patternedContents(400));
	CHECK(entryAbsent(base % "/src/b.bin"));
	CHECK(readFileContents(base % "/dest/src/a.bin") == patternedContents(400));
	CHECK(readFileContents(base % "/dest/src/b.bin") == patternedContents(900));

	REQUIRE(script.seenRequests.size() == 1);
	CHECK(script.seenRequests[0].issue.kind == IssueKind::RootDirectoryMerge);
}

TEST_CASE("move executor: raced read-only state during committed removal", "[moveexecutor][readonly]")
{
	if (readOnlySemanticsUnavailable())
		return;

	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	const QString filePath = base % "/racing.bin";
	writeTestFile(filePath, patternedContents(100));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	// The file is writable through the preflight and the copy; it becomes read-only while the committed
	// removal is held at the barrier, and the forced ReadOnly result stands for what the race would
	// produce. Fresh inspection confirms it, so the item-only read-only question is asked.
	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
	hooks.armBarrier(Point::RemoveEntry_Native);
	hooks.forceNativeError(Point::RemoveEntry_Native, readOnlyCode);

	OperationScript script;
	script.decisions = { act(DecisionAction::MakeWritable) };

	const auto request = makeTransferRequest(TransferKind::Move, { filePath }, DestinationIntent::IntoDirectory, base % "/dest");
	REQUIRE(request.has_value());
	auto context = makeScriptedContext(script, PrimaryProgressUnit::Bytes);
	CTransferExecutor executor{ context };

	OperationSummary summary;
	std::thread worker{ [&] { summary = executor.run(*request); } };

	REQUIRE(hooks.waitForBarrier(Point::RemoveEntry_Native, std::chrono::milliseconds{ 5000 }));
	setFileReadOnly(filePath, true);
	hooks.releaseBarrier(Point::RemoveEntry_Native);
	worker.join();

	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 1);
	CHECK(entryAbsent(filePath));
	CHECK(!entryAbsent(base % "/dest/racing.bin"));

	REQUIRE(script.seenRequests.size() == 1);
	const DecisionRequest& seenRequest = script.seenRequests.front();
	CHECK(seenRequest.issue.kind == IssueKind::ReadOnlySourceRemoval);
	CHECK(!seenRequest.remainingMatchingScopeAllowed); // Item-only in the committed segment
	REQUIRE(seenRequest.issue.failure.has_value());
	CHECK(seenRequest.issue.failure->action == FailedAction::RemovePublishedMoveSource);
}

TEST_CASE("move executor: commit boundary and cancellation", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src"));
	writeTestFile(base % "/src/a.bin", patternedContents(1000));
	writeTestFile(base % "/src/b.bin", patternedContents(1000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

	OperationScript script;

	SECTION("cancellation before publication aborts staging and preserves the source")
	{
		script.cancelAtCheckpoint = [&] { return stagingFileCount(base % "/dest/src") > 0; };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.completedItems == 0);
		CHECK(!entryAbsent(base % "/src/a.bin"));
		CHECK(!entryAbsent(base % "/src/b.bin"));
		CHECK(stagingFileCount(base % "/dest/src") == 0);
		CHECK(entryAbsent(base % "/dest/src/a.bin"));
		CHECK(entryAbsent(base % "/dest/src/b.bin"));
	}

	SECTION("cancellation after publication never leaves a published destination with its source")
	{
		// Cancel at the first checkpoint after either file has been published. The commit discipline
		// requires that file's source removal to have already completed - cancellation may only take
		// effect before the next logical node.
		script.cancelAtCheckpoint = [&] {
			return QFileInfo::exists(base % "/dest/src/a.bin") || QFileInfo::exists(base % "/dest/src/b.bin");
		};
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Cancelled);
		CHECK(summary.completedItems == 1);
		CHECK(summary.failedItems == 0);

		const bool aMoved = QFileInfo::exists(base % "/dest/src/a.bin");
		const QString movedName = aMoved ? QStringLiteral("a.bin") : QStringLiteral("b.bin");
		const QString untouchedName = aMoved ? QStringLiteral("b.bin") : QStringLiteral("a.bin");
		CHECK(entryAbsent(base % "/src/" % movedName)); // Fully moved: source removed with its publication
		CHECK(!entryAbsent(base % "/src/" % untouchedName)); // Fully untouched
		CHECK(entryAbsent(base % "/dest/src/" % untouchedName));
		CHECK(!entryAbsent(base % "/src")); // The cancelled directory is preserved
	}
}

TEST_CASE("move executor: directory timestamps in the fallback", "[moveexecutor]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	constexpr int64_t sourceTimeSeconds = 1'500'000'000;

	OperationScript script;

	SECTION("created destination directories receive source times before the source is removed")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/nested"));
		writeTestFile(base % "/src/nested/f.bin", patternedContents(100));
		REQUIRE(setEntryTimes(base % "/src/nested",
			{ .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = sourceTimeSeconds } }));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(base % "/src"));
		CHECK(entryLastWriteSeconds(base % "/dest/src/nested") == sourceTimeSeconds);
	}

	SECTION("a merge target keeps its own metadata")
	{
		REQUIRE(QDir{}.mkpath(base % "/src"));
		writeTestFile(base % "/src/f.bin", patternedContents(100));
		REQUIRE(setEntryTimes(base % "/src",
			{ .creation = {}, .last_access = {}, .last_write = thin_io::timestamp{ .seconds = sourceTimeSeconds } }));
		REQUIRE(QDir{}.mkpath(base % "/dest/src")); // Occupied: merge

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

		script.decisions = { act(DecisionAction::Merge) };
		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");

		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(entryAbsent(base % "/src"));
		// Child arrival naturally updates the merge target's mtime, so prove only that the source's own
		// stamp was not applied to it.
		CHECK(entryLastWriteSeconds(base % "/dest/src") != sourceTimeSeconds);
	}

	SECTION("a timestamp application failure is a warning and does not block source cleanup")
	{
		REQUIRE(QDir{}.mkpath(base % "/src/nested"));
		writeTestFile(base % "/src/nested/f.bin", patternedContents(100));
		REQUIRE(QDir{}.mkpath(base % "/dest"));

		CFaultHookScope hooks;
		hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);
		hooks.forceNativeError(Point::ApplyDirectoryTimes_Native, accessDeniedCode);

		const auto summary = runMove(script, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest");
		CHECK(summary.status == CompletionStatus::Completed);
		CHECK(summary.warningCount == 1);
		CHECK(summary.failedItems == 0);
		CHECK(entryAbsent(base % "/src")); // Cleanup proceeded regardless
		CHECK(readFileContents(base % "/dest/src/nested/f.bin") == patternedContents(100));
	}
}

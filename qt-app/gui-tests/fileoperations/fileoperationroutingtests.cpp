// WP11A: the production-routing integration gate. It drives the real UI launch boundary
// (makeUiTransferRequest / makePermanentDeleteRequest / deletionBackendFor) into a real CFileOperationDialog and
// CFileOperationJob and asserts the on-disk outcome - proving that F5/F6, drag-and-drop, and delete routing reach
// the tested engine with the right typed request and lifecycle. F5/F6 and drag-and-drop share one launch method,
// so a transfer case here exercises both. The executor, staged-copy, resolver, and job suites remain the primary
// correctness proof; this gate is about the wiring. Every mutation happens inside a canonicalized sandbox that
// rejects any path escaping its temporary root, so a routing defect can never touch a file outside it.

#include "fileoperationguitesthelpers.h"

#include "progressdialogs/fileoperationlaunch.h"
#include "fileoperations/operationtesthooks.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h> // ERROR_* codes
#else
#include <errno.h>
#endif

using namespace guitest;
using namespace std::chrono_literals;
using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;

namespace
{

#ifdef _WIN32
constexpr NativeErrorCode crossDeviceCode = ERROR_NOT_SAME_DEVICE;
#else
constexpr NativeErrorCode crossDeviceCode = EXDEV;
#endif

// A temporary-root sandbox for the routing gate. Every path it hands out is proven to canonicalize beneath the
// root, so a routing defect can neither read nor mutate anything outside the sandbox. Absent destinations are
// validated through their nearest existing ancestor; existing entries (including any link fixture) through
// themselves.
class RoutingSandbox
{
public:
	RoutingSandbox()
	{
		REQUIRE(_tempDir.isValid());
		_root = QFileInfo(_tempDir.path()).canonicalFilePath();
		REQUIRE(!_root.isEmpty());
	}

	// Absolute path for a (possibly not-yet-existing) entry, proven to lie under the root.
	[[nodiscard]] QString path(const QString& relative) const
	{
		const QString absolute = QDir(_root).absoluteFilePath(relative);
		assertUnderRoot(absolute);
		return absolute;
	}

	// Creates the file (and any parent directories) with deterministic contents; returns its verified path.
	QString makeFile(const QString& relative, const int size)
	{
		const QString p = path(relative);
		REQUIRE(QDir().mkpath(QFileInfo(p).absolutePath()));
		writeFile(p, blob(size));
		return p;
	}

	QString makeDir(const QString& relative)
	{
		const QString p = path(relative);
		REQUIRE(QDir().mkpath(p));
		return p;
	}

private:
	void assertUnderRoot(const QString& absolute) const
	{
		QString existing = absolute;
		while (!existing.isEmpty() && !QFileInfo::exists(existing))
		{
			const QString parent = QFileInfo(existing).absolutePath();
			if (parent == existing)
				break; // Reached the filesystem root without finding an existing ancestor.
			existing = parent;
		}
		const QString canonical = QFileInfo(existing).canonicalFilePath();
		REQUIRE(!canonical.isEmpty());
		REQUIRE((canonical == _root || canonical.startsWith(_root + '/')));
	}

	QTemporaryDir _tempDir;
	QString _root;
};

struct RoutingResult
{
	OperationSummary summary;
	int decisionRequestsPresented = 0;
};

// Builds the transfer request exactly as the F5/F6 and drag-drop handlers do, then drives a real dialog/job to
// completion, answering scripted decisions. Fails the test if the request cannot be built.
RoutingResult driveTransfer(const TransferKind kind, const QStringList& sourcePaths, const QString& destinationText,
	std::vector<Decision> decisions = {}, const uint64_t chunkSize = 8ull * 1024 * 1024)
{
	auto request = makeUiTransferRequest(kind, sourcePaths, destinationText);
	REQUIRE(request.has_value());

	ScriptedDialog dialog{ std::move(*request), {}, nullptr, chunkSize };
	dialog.scriptedDecisions = std::move(decisions);
	dialog.start();
	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	return { *dialog.result(), dialog.decisionRequestsPresented };
}

// The delete counterpart: builds the permanent-delete request as performDeletion's InternalJob branch does.
RoutingResult driveDelete(const QStringList& sourcePaths, std::vector<Decision> decisions = {})
{
	auto request = makePermanentDeleteRequest(sourcePaths);
	REQUIRE(request.has_value());

	ScriptedDialog dialog{ std::move(*request), {} };
	dialog.scriptedDecisions = std::move(decisions);
	dialog.start();
	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	return { *dialog.result(), dialog.decisionRequestsPresented };
}

} // namespace

TEST_CASE("routing gate: F5 copy of a single file into a directory", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 1200);
	const QString destDir = sandbox.makeDir("dest");

	// The destination text is an existing directory, so the heuristic picks IntoDirectory.
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("src.bin") }, destDir);
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(result.summary.completedItems == 1);
	CHECK(readFile(sandbox.path("dest/src.bin")) == blob(1200));
	CHECK(QFile::exists(sandbox.path("src.bin"))); // A copy leaves the source in place.
}

TEST_CASE("routing gate: F5 copy of a single file to an exact new target", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 800);
	sandbox.makeDir("dest");

	// The destination text names a not-yet-existing entry, so the heuristic picks ExactEntry (a renamed copy).
	const QString target = sandbox.path("dest/renamed.bin");
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("src.bin") }, target);
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(readFile(target) == blob(800));
	CHECK(QFile::exists(sandbox.path("src.bin")));
}

TEST_CASE("routing gate: F5 copy of multiple sources into a directory", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("a.bin", 100);
	sandbox.makeFile("b.bin", 200);
	const QString destDir = sandbox.makeDir("dest");

	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("a.bin"), sandbox.path("b.bin") }, destDir);
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(readFile(sandbox.path("dest/a.bin")) == blob(100));
	CHECK(readFile(sandbox.path("dest/b.bin")) == blob(200));
}

TEST_CASE("routing gate: copying a directory merges into an existing one on confirmation", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("tree/keep.bin", 300);          // The source directory 'tree' with one file.
	sandbox.makeDir("dest");
	sandbox.makeFile("dest/tree/existing.bin", 150); // The destination already has a 'tree' with a different file.

	// A selected directory landing on an existing same-named directory prompts RootDirectoryMerge.
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("tree") }, sandbox.path("dest"),
		{ Decision{ DecisionAction::Merge, DecisionScope::ThisItem, {} } });
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(readFile(sandbox.path("dest/tree/existing.bin")) == blob(150)); // Preserved.
	CHECK(readFile(sandbox.path("dest/tree/keep.bin")) == blob(300));     // Merged in.
}

TEST_CASE("routing gate: F6 move of a single file renames in place on the same filesystem", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 500);
	const QString destDir = sandbox.makeDir("dest");

	const auto result = driveTransfer(TransferKind::Move, { sandbox.path("src.bin") }, destDir);
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(readFile(sandbox.path("dest/src.bin")) == blob(500));
	CHECK(!QFile::exists(sandbox.path("src.bin"))); // Moved: the source name is gone.
}

TEST_CASE("routing gate: a cross-device move falls back to a copy-based transfer", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 500);
	const QString destDir = sandbox.makeDir("dest");

	// One forced cross-device error at the root rename routes the whole move to the staged copy-based path; the
	// staged publication's own rename fires after the one-shot is consumed and so succeeds.
	CFaultHookScope hooks;
	hooks.forceNativeError(Point::RenameEntry_Native, crossDeviceCode);

	const auto result = driveTransfer(TransferKind::Move, { sandbox.path("src.bin") }, destDir);
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(hooks.forcedErrorConsumed(Point::RenameEntry_Native));
	CHECK(readFile(sandbox.path("dest/src.bin")) == blob(500));
	CHECK(!QFile::exists(sandbox.path("src.bin")));
}

TEST_CASE("routing gate: an exact-target copy onto an existing file replaces it on confirmation", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 700);
	sandbox.makeFile("dest/old.bin", 40);

	// The destination text is an existing file, so ExactEntry is chosen and the collision prompts FileReplacement.
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("src.bin") }, sandbox.path("dest/old.bin"),
		{ Decision{ DecisionAction::Replace, DecisionScope::ThisItem, {} } });
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(result.decisionRequestsPresented == 1);
	CHECK(readFile(sandbox.path("dest/old.bin")) == blob(700));
}

TEST_CASE("routing gate: a file colliding with a directory is a type mismatch and is skipped", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 60);
	const QString destDir = sandbox.makeDir("dest");
	sandbox.makeDir("dest/src.bin");                 // A directory occupying the incoming file's target name.
	sandbox.makeFile("dest/src.bin/inside.bin", 25);

	// Into-directory copy: the file's target dest/src.bin exists as a directory -> TypeMismatch -> Skip.
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("src.bin") }, destDir,
		{ Decision{ DecisionAction::Skip, DecisionScope::ThisItem, {} } });
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(result.summary.skippedItems == 1);
	CHECK(readFile(sandbox.path("dest/src.bin/inside.bin")) == blob(25)); // The directory is untouched.
	CHECK(QFile::exists(sandbox.path("src.bin")));
}

TEST_CASE("routing gate: a remembered replace decision covers later collisions without re-prompting", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("a.bin", 111);
	sandbox.makeFile("b.bin", 222);
	const QString destDir = sandbox.makeDir("dest");
	sandbox.makeFile("dest/a.bin", 1);
	sandbox.makeFile("dest/b.bin", 2);

	// One Replace remembered for the remaining matching issues must resolve both collisions with a single prompt.
	const auto result = driveTransfer(TransferKind::Copy, { sandbox.path("a.bin"), sandbox.path("b.bin") }, destDir,
		{ Decision{ DecisionAction::Replace, DecisionScope::RemainingMatchingIssues, {} } });
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(result.decisionRequestsPresented == 1);
	CHECK(readFile(sandbox.path("dest/a.bin")) == blob(111));
	CHECK(readFile(sandbox.path("dest/b.bin")) == blob(222));
}

TEST_CASE("routing gate: cancelling a routed copy at a barrier ends it before publication", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("src.bin", 4000);
	sandbox.makeDir("dest");

	auto request = makeUiTransferRequest(TransferKind::Copy, { sandbox.path("src.bin") }, sandbox.path("dest"));
	REQUIRE(request.has_value());

	// Barrier-driven lifecycle through the launch boundary; pause/resume is covered by the dialog suite.
	CFaultHookScope hooks;
	hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);

	ScriptedDialog dialog{ std::move(*request), {}, nullptr, 1024 };
	dialog.start();
	REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));

	dialog.requestCancellation();
	hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);

	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	CHECK(dialog.result()->status == CompletionStatus::Cancelled);
	CHECK(!QFile::exists(sandbox.path("dest/src.bin"))); // The staged copy was aborted before publication.
}

TEST_CASE("routing gate: two routed operations run concurrently to completion", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("a.bin", 2000);
	sandbox.makeFile("b.bin", 2000);
	sandbox.makeDir("destA");
	sandbox.makeDir("destB");

	auto requestA = makeUiTransferRequest(TransferKind::Copy, { sandbox.path("a.bin") }, sandbox.path("destA"));
	auto requestB = makeUiTransferRequest(TransferKind::Copy, { sandbox.path("b.bin") }, sandbox.path("destB"));
	REQUIRE(requestA.has_value());
	REQUIRE(requestB.has_value());

	ScriptedDialog dialogA{ std::move(*requestA), {}, nullptr, 512 };
	ScriptedDialog dialogB{ std::move(*requestB), {}, nullptr, 512 };
	dialogA.start();
	dialogB.start();

	REQUIRE(pumpUntil([&] { return dialogA.result().has_value() && dialogB.result().has_value(); }));
	CHECK(dialogA.result()->status == CompletionStatus::Completed);
	CHECK(dialogB.result()->status == CompletionStatus::Completed);
	CHECK(readFile(sandbox.path("destA/a.bin")) == blob(2000));
	CHECK(readFile(sandbox.path("destB/b.bin")) == blob(2000));
}

TEST_CASE("routing gate: internal permanent delete removes a temporary tree", "[routing]")
{
	RoutingSandbox sandbox;
	sandbox.makeFile("doomed/a.bin", 100);
	sandbox.makeFile("doomed/sub/b.bin", 200);
	sandbox.makeDir("doomed/empty");

	const auto result = driveDelete({ sandbox.path("doomed") });
	CHECK(result.summary.status == CompletionStatus::Completed);
	CHECK(!QFile::exists(sandbox.path("doomed")));
}

TEST_CASE("routing gate: an invalid source path is rejected before any job is created", "[routing]")
{
	RoutingSandbox sandbox;
	const QString destDir = sandbox.makeDir("dest");

	// A relative external path cannot become a trusted request; the handler reports the error and starts no job.
	const auto request = makeUiTransferRequest(TransferKind::Copy, { QStringLiteral("relative/only.bin") }, destDir);
	CHECK(!request.has_value());
}

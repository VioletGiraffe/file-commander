// WP10B: the one internal-operation dialog driving a real CFileOperationJob - event rendering, decision
// dispatch, pause/cancel via deterministic hook barriers, background placement, and completion.

#include "fileoperationguitesthelpers.h"

#include "progressdialogs/cfileoperationdialog.h"

#include "fileoperations/operationtesthooks.h"

DISABLE_COMPILER_WARNINGS
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStringBuilder>
#include <QTemporaryDir>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include "3rdparty/catch2/catch.hpp"

#include <chrono>
#include <thread>

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;
using namespace std::chrono_literals;
using namespace guitest;

namespace
{

TransferRequest copyInto(const QString& source, const QString& destinationDir)
{
	return TransferRequest{ TransferKind::Copy, { entryPath(source) },
		DestinationSpec{ DestinationIntent::IntoDirectory, entryPath(destinationDir) } };
}

TransferRequest copyOnto(const QString& source, const QString& destination)
{
	return TransferRequest{ TransferKind::Copy, { entryPath(source) },
		DestinationSpec{ DestinationIntent::ExactEntry, entryPath(destination) } };
}

} // namespace

TEST_CASE("dialog: renders the scanning, byte, and item phases", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(1000));

	// The job is constructed but never started here: renderProgress is exercised with synthetic snapshots.
	ScriptedDialog dialog{ copyInto(base % "/src.bin", base), {} };

	SECTION("scanning is indeterminate with a discovered count and no ETA")
	{
		dialog.renderProgress(ProgressSnapshot{ .phase = OperationPhase::Scanning, .itemsProcessed = 7 });
		auto* overall = dialog.findChild<QProgressBar*>(QStringLiteral("_overallProgress"));
		REQUIRE(overall != nullptr);
		CHECK(overall->maximum() == 0); // Qt's convention for an indeterminate bar
		CHECK(label(dialog, "_lblStatus")->text().contains(QStringLiteral("7")));
		CHECK(label(dialog, "_lblStatus")->text().contains(QStringLiteral("Scanning")));
	}

	SECTION("working with a known byte total shows a determinate percentage")
	{
		dialog.renderProgress(ProgressSnapshot{ .phase = OperationPhase::Working, .bytesProcessed = 250, .bytesTotal = 1000,
			.currentEntryBytesProcessed = 250, .currentEntryBytesTotal = 1000, .primaryUnitsPerSecond = 5000, .secondsRemaining = 3 });
		auto* overall = dialog.findChild<QProgressBar*>(QStringLiteral("_overallProgress"));
		CHECK(overall->maximum() == 100);
		CHECK(overall->value() == 25);
		CHECK(dialog.findChild<QProgressBar*>(QStringLiteral("_fileProgress"))->value() == 25);
		CHECK(label(dialog, "_lblStatus")->text().contains(QStringLiteral("remaining"))); // ETA rendered
	}

	SECTION("working with unknown totals stays indeterminate")
	{
		dialog.renderProgress(ProgressSnapshot{ .phase = OperationPhase::Working, .bytesProcessed = 250 });
		CHECK(dialog.findChild<QProgressBar*>(QStringLiteral("_overallProgress"))->maximum() == 0);
		CHECK(label(dialog, "_lblStatus")->text().isEmpty() == false);
	}

	SECTION("a known total but no ETA omits the remaining-time clause")
	{
		dialog.renderProgress(ProgressSnapshot{ .phase = OperationPhase::Working, .bytesProcessed = 100, .bytesTotal = 1000,
			.primaryUnitsPerSecond = 5000 });
		CHECK(!label(dialog, "_lblStatus")->text().contains(QStringLiteral("remaining")));
	}
}

TEST_CASE("dialog: a delete configures the item unit and hides the per-file bar", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/doomed.bin", blob(500));

	ScriptedDialog dialog{ PermanentDeleteRequest{ { entryPath(base % "/doomed.bin") } }, {} };
	CHECK(dialog.findChild<QProgressBar*>(QStringLiteral("_fileProgress"))->isHidden());

	// An item-phase snapshot drives the overall bar by item count.
	dialog.renderProgress(ProgressSnapshot{ .phase = OperationPhase::Working, .itemsProcessed = 1, .itemsTotal = 2,
		.primaryUnitsPerSecond = 4 });
	CHECK(dialog.findChild<QProgressBar*>(QStringLiteral("_overallProgress"))->value() == 50);
}

TEST_CASE("dialog: a copy runs to completion", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeFile(base % "/src/a.bin", blob(3000));
	writeFile(base % "/src/sub/b.bin", blob(4000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	ScriptedDialog dialog{ copyInto(base % "/src", base % "/dest"), {}, nullptr, 1024 };
	dialog.start();
	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));

	const OperationSummary& summary = *dialog.result();
	CHECK(summary.status == CompletionStatus::Completed);
	CHECK(summary.completedItems == 4); // src, a.bin, sub, b.bin
	CHECK(dialog.decisionRequestsPresented == 0);

	CHECK(QFile::exists(base % "/dest/src/a.bin"));
	CHECK(QFile::exists(base % "/dest/src/sub/b.bin"));

	// Completion presentation: the summary is shown and the Cancel button becomes Close.
	CHECK(!label(dialog, "_lblSummary")->isHidden());
	CHECK(dialog.findChild<QPushButton*>(QStringLiteral("_btnCancel"))->text() == QStringLiteral("Close"));
	CHECK(dialog.findChild<QPushButton*>(QStringLiteral("_btnPause"))->isHidden());
}

TEST_CASE("dialog: a replacement prompt is dispatched and answered", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(700));
	writeFile(base % "/dest.bin", blob(50));

	ScriptedDialog dialog{ copyOnto(base % "/src.bin", base % "/dest.bin"), {}, nullptr, 1024 };
	dialog.scriptedDecisions = { Decision{ DecisionAction::Replace, DecisionScope::ThisItem, {} } };
	dialog.start();

	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	CHECK(dialog.decisionRequestsPresented == 1);
	CHECK(dialog.lastRequestKind == IssueKind::FileReplacement);
	CHECK(dialog.result()->status == CompletionStatus::Completed);
	QFile replaced{ base % "/dest.bin" };
	REQUIRE(replaced.open(QFile::ReadOnly));
	CHECK(replaced.readAll() == blob(700));
}

TEST_CASE("dialog: cancellation invalidates an undrained decision without presenting it", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(700));
	writeFile(base % "/dest.bin", blob(50));

	ScriptedDialog dialog{ copyOnto(base % "/src.bin", base % "/dest.bin"), {}, nullptr, 1024 };
	dialog.start();

	// Stop the auto-timer before its first tick so the collision decision is never drained into a prompt;
	// cancellation then wins the worker's decision wait (whether the request was already queued or not).
	dialog.findChild<QTimer*>(QStringLiteral("eventTimer"))->stop();
	dialog.requestCancellation();
	dialog.drainEvents(); // Dispatches whatever is queued; no decision must be presented
	CHECK(dialog.decisionRequestsPresented == 0);

	dialog.findChild<QTimer*>(QStringLiteral("eventTimer"))->start(); // No decision remains to present; let the drain deliver completion
	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	CHECK(dialog.result()->status == CompletionStatus::Cancelled);
	QFile untouched{ base % "/dest.bin" };
	REQUIRE(untouched.open(QFile::ReadOnly));
	CHECK(untouched.readAll() == blob(50)); // The replacement was never authorized
}

TEST_CASE("dialog: pause holds the worker; resume finishes", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(4000));

	CFaultHookScope hooks;
	hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);

	ScriptedDialog dialog{ copyOnto(base % "/src.bin", base % "/dest.bin"), {}, nullptr, 1024 };
	dialog.start();
	REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));

	dialog.setPaused(true);
	hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);

	// One-sided: a working pause means no chunk streams. A broken pause fails; a slow machine never false-fails.
	std::this_thread::sleep_for(150ms);
	QCoreApplication::processEvents();
	CHECK(hooks.arrivalCount(Point::StagedCopy_WriteStaging_Native) == 0);

	dialog.setPaused(false);
	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	CHECK(dialog.result()->status == CompletionStatus::Completed);
}

TEST_CASE("dialog: cancel while held at a barrier ends the operation", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(4000));

	CFaultHookScope hooks;
	hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);

	ScriptedDialog dialog{ copyOnto(base % "/src.bin", base % "/dest.bin"), {}, nullptr, 1024 };
	dialog.start();
	REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));

	dialog.requestCancellation();
	hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);

	REQUIRE(pumpUntil([&dialog] { return dialog.result().has_value(); }));
	CHECK(dialog.result()->status == CompletionStatus::Cancelled);
	CHECK(!QFile::exists(base % "/dest.bin")); // The staged copy was aborted before publication
}

TEST_CASE("dialog: background mode repositions to the injected anchor", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(500));

	const QPoint anchor{ 640, 480 };
	ScriptedDialog dialog{ copyInto(base % "/src.bin", base), [anchor] { return anchor; } };
	dialog.show();

	CHECK(!dialog.isInBackgroundMode());
	dialog.findChild<QPushButton*>(QStringLiteral("_btnBackground"))->click();

	// The reposition is posted through a queued call; let it run.
	REQUIRE(pumpUntil([&dialog] { return dialog.isInBackgroundMode(); }, 2s));
	CHECK(dialog.x() == anchor.x());
	CHECK(dialog.y() + dialog.height() == anchor.y()); // The anchor is the bottom-left corner
	CHECK(dialog.findChild<QPushButton*>(QStringLiteral("_btnBackground"))->isHidden());
}

TEST_CASE("dialog: the cancel confirmation blocks decision presentation until dismissed", "[fileoperationdialog]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();
	writeFile(base % "/src.bin", blob(700));
	writeFile(base % "/dest.bin", blob(50)); // Collides: the worker will block awaiting a decision

	ScriptedDialog dialog{ copyOnto(base % "/src.bin", base % "/dest.bin"), {}, nullptr, 1024 };
	dialog.scriptedDecisions = { Decision{ DecisionAction::Skip, DecisionScope::ThisItem, {} } };
	dialog.start();

	// Draining is driven manually from here so the prompt cannot be presented before the confirmation opens.
	auto* eventTimer = dialog.findChild<QTimer*>(QStringLiteral("eventTimer"));
	REQUIRE(eventTimer != nullptr);
	eventTimer->stop();

	// The worker needs no main-thread service to reach its decision wait and queue the request.
	std::this_thread::sleep_for(std::chrono::milliseconds{ 300 });

	int drainAttemptsDuringConfirm = 0;
	QTimer poller;
	poller.setInterval(25);
	QObject::connect(&poller, &QTimer::timeout, &poller, [&] {
		auto* box = dialog.findChild<QMessageBox*>();
		if (box == nullptr || !box->isVisible())
			return;
		dialog.drainEvents(); // Must be swallowed by the guard while the confirmation is up
		if (++drainAttemptsDuringConfirm >= 3)
		{
			poller.stop();
			box->button(QMessageBox::No)->click();
		}
	});
	poller.start();

	dialog.findChild<QPushButton*>(QStringLiteral("_btnCancel"))->click(); // Spins the confirmation's nested loop

	CHECK(drainAttemptsDuringConfirm >= 3);
	CHECK(dialog.decisionRequestsPresented == 0); // The queued prompt never surfaced over the confirmation
	CHECK(!dialog.result().has_value());

	// With the confirmation declined, draining presents the pending prompt and the operation finishes.
	REQUIRE(pumpUntil([&dialog] { dialog.drainEvents(); return dialog.result().has_value(); }));
	CHECK(dialog.decisionRequestsPresented == 1);
	CHECK(dialog.result()->status == CompletionStatus::Completed);
	CHECK(dialog.result()->skippedItems == 1);
	CHECK(readFile(base % "/dest.bin") == blob(50)); // Skip left the destination untouched
}

TEST_CASE("dialog: two operations run simultaneously", "[fileoperationdialog]")
{
	QTemporaryDir tempA, tempB;
	REQUIRE(tempA.isValid());
	REQUIRE(tempB.isValid());

	writeFile(tempA.path() % "/a.bin", blob(20000));
	REQUIRE(QDir{}.mkpath(tempA.path() % "/dest"));
	writeFile(tempB.path() % "/b.bin", blob(24000));
	REQUIRE(QDir{}.mkpath(tempB.path() % "/dest"));

	ScriptedDialog first{ copyInto(tempA.path() % "/a.bin", tempA.path() % "/dest"), {}, nullptr, 1024 };
	ScriptedDialog second{ copyInto(tempB.path() % "/b.bin", tempB.path() % "/dest"), {}, nullptr, 1024 };
	first.start();
	second.start();

	REQUIRE(pumpUntil([&] { return first.result().has_value() && second.result().has_value(); }));
	CHECK(first.result()->status == CompletionStatus::Completed);
	CHECK(second.result()->status == CompletionStatus::Completed);
	CHECK(QFile::exists(tempA.path() % "/dest/a.bin"));
	CHECK(QFile::exists(tempB.path() % "/dest/b.bin"));
}

TEST_CASE("dialog: completion text summarizes the outcome", "[fileoperationdialog]")
{
	SECTION("mixed counts list every non-zero fact")
	{
		const OperationSummary summary{ .status = CompletionStatus::Completed, .completedItems = 5, .skippedItems = 2,
			.failedItems = 1, .alreadySatisfiedItems = 3, .warningCount = 4 };
		const QString text = CFileOperationDialog::composeSummaryText(summary, PromptOperation::Copy);
		CHECK(text.contains(QStringLiteral("5 items copied")));
		CHECK(text.contains(QStringLiteral("2 skipped")));
		CHECK(text.contains(QStringLiteral("1 failed")));
		CHECK(text.contains(QStringLiteral("3 already up to date")));
		CHECK(text.contains(QStringLiteral("4 warnings")));
	}

	SECTION("an all-already-satisfied run reads as nothing needed")
	{
		const OperationSummary summary{ .status = CompletionStatus::Completed, .alreadySatisfiedItems = 2 };
		const QString text = CFileOperationDialog::composeSummaryText(summary, PromptOperation::Move);
		CHECK(text.contains(QStringLiteral("Nothing needed to be moved")));
	}

	SECTION("cancellation is stated")
	{
		const OperationSummary summary{ .status = CompletionStatus::Cancelled, .completedItems = 1 };
		CHECK(CFileOperationDialog::composeSummaryText(summary, PromptOperation::Delete).contains(QStringLiteral("cancelled")));
	}

	SECTION("representative failures are listed with the affected entry and reason")
	{
#ifdef _WIN32
		const CEntryPath failedEntry = entryPath(QStringLiteral("C:/somewhere/stuck.bin"));
#else
		const CEntryPath failedEntry = entryPath(QStringLiteral("/somewhere/stuck.bin"));
#endif
		OperationSummary summary{ .status = CompletionStatus::Failed, .failedItems = 1 };
		summary.representativeFailures.push_back(OperationDiagnostic{
			FailureDetails{ FailedAction::RemovePublishedMoveSource, CFileSystemError{ FileErrorCategory::PermissionDenied, 5, {} } },
			EntrySnapshot{ failedEntry, OperationEntryKind::RegularFile, 0 }, {} });

		const QString text = CFileOperationDialog::composeSummaryText(summary, PromptOperation::Move);
		CHECK(text.contains(QStringLiteral("stuck.bin")));
		CHECK(text.contains(QStringLiteral("access denied")));
	}

	SECTION("representative warnings are listed with the affected entry and reason")
	{
#ifdef _WIN32
		const CEntryPath warnedEntry = entryPath(QStringLiteral("C:/somewhere/slowdir"));
#else
		const CEntryPath warnedEntry = entryPath(QStringLiteral("/somewhere/slowdir"));
#endif
		OperationSummary summary{ .status = CompletionStatus::Completed, .completedItems = 3, .warningCount = 1 };
		summary.representativeWarnings.push_back(OperationDiagnostic{
			FailureDetails{ FailedAction::PreserveDirectoryTimestamps, CFileSystemError{ FileErrorCategory::PermissionDenied, 5, {} } },
			EntrySnapshot{ warnedEntry, OperationEntryKind::Directory, 0 }, {} });

		const QString text = CFileOperationDialog::composeSummaryText(summary, PromptOperation::Copy);
		CHECK(text.contains(QStringLiteral("1 warnings")));
		CHECK(text.contains(QStringLiteral("slowdir")));
		CHECK(text.contains(QStringLiteral("access denied")));
	}
}

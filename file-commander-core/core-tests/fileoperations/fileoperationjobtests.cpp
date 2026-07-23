// WP9: CFileOperationJob - the one owner of cross-thread state around the synchronous executors:
// pause/cancel/decision wakeups, the coalescing event queue, and the listener boundary.

#include "fileoperations/cfileoperationjob.h"
#include "fileoperations/operationtesthooks.h"

#include "fileoperationtesthelpers.h"

DISABLE_COMPILER_WARNINGS
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <thread>

using OperationTestHooks::CFaultHookScope;
using OperationTestHooks::Point;
using namespace std::chrono_literals;

namespace
{

[[nodiscard]] bool waitUntil(const std::function<bool()>& condition, const std::chrono::milliseconds timeout = 10s)
{
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (!condition())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::sleep_for(1ms);
	}
	return true;
}

[[nodiscard]] TransferRequest transferRequest(const TransferKind kind, const QStringList& sources, const DestinationIntent intent, const QString& destination)
{
	auto request = makeTransferRequest(kind, sources, intent, destination);
	REQUIRE(request.has_value());
	return *request;
}

[[nodiscard]] PermanentDeleteRequest deleteRequest(const QStringList& sources)
{
	auto request = makePermanentDeleteRequest(sources);
	REQUIRE(request.has_value());
	return *request;
}

// Drives the job the way the dialog timer does: polls processEvents, collects every event, answers
// scripted decisions from inside the listener callback (which doubles as constant proof that the queue
// mutex is released during dispatch), and cancels on an unscripted prompt so an unexpected decision
// cannot masquerade as a worker hang.
struct JobDriver final : CFileOperationListener
{
	explicit JobDriver(CFileOperationJob& jobUnderTest) noexcept : job{ jobUnderTest } {}

	void onOperationEvent(const OperationEvent& event) override
	{
		events.push_back(event);
		const auto* request = std::get_if<DecisionRequest>(&event);
		if (!request)
			return;

		if (onDecisionRequest)
			onDecisionRequest(*request);
		if (nextDecision < decisions.size())
			job.submitDecision(decisions[nextDecision++]);
		else if (autoCancelUnscripted)
		{
			unscriptedRequestSeen = true;
			job.cancel();
		}
	}

	// Polls until the summary event has been delivered.
	[[nodiscard]] bool pumpToCompletion(const std::chrono::milliseconds timeout = 10s)
	{
		return waitUntil([this] {
			job.processEvents(*this);
			return summary() != nullptr;
		}, timeout);
	}

	[[nodiscard]] const OperationSummary* summary() const
	{
		for (const OperationEvent& event : events)
			if (const auto* found = std::get_if<OperationSummary>(&event); found)
				return found;
		return nullptr;
	}

	[[nodiscard]] size_t decisionRequestCount() const
	{
		size_t count = 0;
		for (const OperationEvent& event : events)
			if (std::holds_alternative<DecisionRequest>(event))
				++count;
		return count;
	}

	[[nodiscard]] size_t summaryCount() const
	{
		size_t count = 0;
		for (const OperationEvent& event : events)
			if (std::holds_alternative<OperationSummary>(event))
				++count;
		return count;
	}

	CFileOperationJob& job;
	std::vector<OperationEvent> events;
	std::vector<Decision> decisions;
	size_t nextDecision = 0;
	bool autoCancelUnscripted = true;
	bool unscriptedRequestSeen = false;
	std::function<void(const DecisionRequest&)> onDecisionRequest; // Runs before the scripted answer
};

} // namespace

TEST_CASE("job: copy runs to completion with ordered events", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/src/sub"));
	writeTestFile(base % "/src/a.bin", patternedContents(3000));
	writeTestFile(base % "/src/sub/b.bin", patternedContents(5000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src" }, DestinationIntent::IntoDirectory, base % "/dest"), 1024 };
	JobDriver driver{ job };

	CHECK(job.status() == JobStatus::NotStarted);
	CHECK(!job.hasPendingDecision());
	CHECK(!job.submitDecision(act(DecisionAction::Skip))); // Nothing is pending before the run

	job.start();
	REQUIRE(driver.pumpToCompletion());

	CHECK(job.status() == JobStatus::Finished);
	const OperationSummary* summary = driver.summary();
	CHECK(summary->status == CompletionStatus::Completed);
	CHECK(summary->completedItems == 4); // src, a.bin, sub, b.bin
	CHECK(summary->transferredBytes == 8000);
	CHECK(!driver.unscriptedRequestSeen);
	requireEqualTrees(base % "/src", base % "/dest/src");

	// Exactly one summary, as the last event; everything before it is progress.
	CHECK(driver.summaryCount() == 1);
	CHECK(driver.decisionRequestCount() == 0);
	REQUIRE(!driver.events.empty());
	CHECK(std::holds_alternative<OperationSummary>(driver.events.back()));
	bool sawTransferProgress = false;
	for (const OperationEvent& event : driver.events)
	{
		if (const auto* progress = std::get_if<ProgressSnapshot>(&event); progress && progress->bytesProcessed > 0)
			sawTransferProgress = true;
	}
	CHECK(sawTransferProgress);

	// The queue was fully drained with the summary; nothing arrives afterwards.
	const size_t eventCount = driver.events.size();
	job.processEvents(driver);
	CHECK(driver.events.size() == eventCount);
	CHECK(!job.submitDecision(act(DecisionAction::Skip))); // Late responses stay rejected after completion
}

TEST_CASE("job: delete routes to the delete executor", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	REQUIRE(QDir{}.mkpath(base % "/victim/sub"));
	writeTestFile(base % "/victim/a.bin", patternedContents(100));
	writeTestFile(base % "/victim/sub/b.bin", patternedContents(200));

	CFileOperationJob job{ deleteRequest({ base % "/victim" }) };
	JobDriver driver{ job };
	job.start();
	REQUIRE(driver.pumpToCompletion());

	const OperationSummary* summary = driver.summary();
	CHECK(summary->status == CompletionStatus::Completed);
	CHECK(summary->completedItems == 4);
	CHECK(summary->transferredBytes == 0);
	CHECK(!driver.unscriptedRequestSeen);
	CHECK(entryAbsent(base % "/victim"));
}

TEST_CASE("job: status derivation and summary-before-Finished ordering", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(2000));

	CFaultHookScope hooks;
	hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	job.start();

	REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));
	CHECK(job.status() == JobStatus::Running);

	hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);
	REQUIRE(waitUntil([&job] { return job.status() == JobStatus::Finished; }));

	// Finished is flipped after the summary is queued, so one drain with no further polling must deliver it.
	JobDriver driver{ job };
	job.processEvents(driver);
	REQUIRE(driver.summaryCount() == 1);
	CHECK(driver.summary()->status == CompletionStatus::Completed);
	CHECK(readFileContents(base % "/dest.bin") == patternedContents(2000));
}

TEST_CASE("job: cancellation after Finished is inert", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(1000));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	JobDriver driver{ job };
	job.start();

	SECTION("after the summary was drained: status stays Finished and no further events appear")
	{
		REQUIRE(driver.pumpToCompletion());
		REQUIRE(driver.summary()->status == CompletionStatus::Completed);

		job.cancel();
		CHECK(job.status() == JobStatus::Finished);
		job.processEvents(driver);
		CHECK(driver.summaryCount() == 1);
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(1000));
	}

	SECTION("between the worker finishing and the drain: the queued summary survives the cancel")
	{
		REQUIRE(waitUntil([&job] { return job.status() == JobStatus::Finished; }));

		job.cancel(); // Scrubs only decision events; the summary must still be deliverable
		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.summaryCount() == 1);
		CHECK(driver.summary()->status == CompletionStatus::Completed);
		CHECK(driver.summary()->completedItems == 1);
	}
}

TEST_CASE("job: pause and resume", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(300'000));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	JobDriver driver{ job };

	SECTION("pausing before start holds the worker at the first checkpoint; resume completes the job")
	{
		job.setPaused(true);
		job.setPaused(true); // Idempotent
		job.start();

		std::this_thread::sleep_for(50ms);
		CHECK(job.status() == JobStatus::Running);
		CHECK(entryAbsent(base % "/dest.bin")); // One-sided check: a broken pause would have copied by now

		job.setPaused(false);
		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.summary()->status == CompletionStatus::Completed);
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(300'000));
	}

	SECTION("a pause requested mid-file blocks before the next chunk")
	{
		CFaultHookScope hooks;
		hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);
		job.start();
		REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));

		// The worker is past the file's entry checkpoint; the next checkpoint precedes the first chunk write.
		job.setPaused(true);
		hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);

		std::this_thread::sleep_for(100ms);
		CHECK(hooks.arrivalCount(Point::StagedCopy_WriteStaging_Native) == 0); // One-sided: no chunk may stream while paused

		job.setPaused(false);
		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.summary()->status == CompletionStatus::Completed);
	}

	SECTION("cancellation wakes a paused worker")
	{
		CFaultHookScope hooks;
		hooks.armBarrier(Point::StagedCopy_CreateStaging_Native);
		job.start();
		REQUIRE(hooks.waitForBarrier(Point::StagedCopy_CreateStaging_Native, 5s));

		job.setPaused(true);
		hooks.releaseBarrier(Point::StagedCopy_CreateStaging_Native);
		job.cancel();

		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.summary()->status == CompletionStatus::Cancelled);
		CHECK(entryAbsent(base % "/dest.bin"));
		CHECK(stagingFileCount(base) == 0); // The cancelled session aborted and cleaned its staging file
	}
}

TEST_CASE("job: decision flow", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(700));
	writeTestFile(base % "/dest.bin", patternedContents(50));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	JobDriver driver{ job };

	SECTION("a replacement prompt is delivered, answered, and honored")
	{
		driver.decisions = { act(DecisionAction::Replace) };
		bool pendingVisibleInsideCallback = false;
		driver.onDecisionRequest = [&](const DecisionRequest& request) {
			pendingVisibleInsideCallback = job.hasPendingDecision(); // Callback runs with the mutex released
			CHECK(request.issue.kind == IssueKind::FileReplacement);
		};

		job.start();
		REQUIRE(driver.pumpToCompletion());

		CHECK(pendingVisibleInsideCallback);
		CHECK(driver.decisionRequestCount() == 1);
		CHECK(driver.summary()->status == CompletionStatus::Completed);
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(700));
		CHECK(!job.hasPendingDecision());
	}

	SECTION("cancellation while a decision is pending suppresses the undrained event")
	{
		job.start();
		REQUIRE(waitUntil([&job] { return job.hasPendingDecision(); }));

		job.cancel();
		CHECK(!job.hasPendingDecision()); // Invalidated immediately, before the worker even wakes
		CHECK(!job.submitDecision(act(DecisionAction::Replace))); // The late response is rejected

		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.decisionRequestCount() == 0); // Scrubbed from the queue, never presented
		CHECK(driver.summary()->status == CompletionStatus::Cancelled);
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(50)); // Replacement never authorized
	}

	SECTION("cancellation after the event was drained: the pre-show query suppresses presentation")
	{
		driver.autoCancelUnscripted = false; // This test manages the prompt lifecycle itself

		job.start();
		REQUIRE(waitUntil([&job] { return job.hasPendingDecision(); }));
		job.processEvents(driver); // The DecisionRequest is now out of the queue, in the dialog's hands
		REQUIRE(driver.decisionRequestCount() == 1);

		job.cancel();
		CHECK(!job.hasPendingDecision()); // The dialog's pre-show check now vetoes the prompt
		CHECK(!job.submitDecision(act(DecisionAction::Replace)));

		REQUIRE(driver.pumpToCompletion());
		CHECK(driver.summary()->status == CompletionStatus::Cancelled);
		CHECK(readFileContents(base % "/dest.bin") == patternedContents(50));
	}
}

TEST_CASE("job: a decision outside the delivered request is rejected", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(700));
	writeTestFile(base % "/dest.bin", patternedContents(50));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	job.start();
	REQUIRE(waitUntil([&job] { return job.hasPendingDecision(); }));

	// FileReplacement never offers Merge; the rejection logs a recoverable assert and answers nothing.
	CHECK(!job.submitDecision(act(DecisionAction::Merge)));
	CHECK(job.hasPendingDecision()); // The prompt is still live and answerable

	CHECK(job.submitDecision(act(DecisionAction::Skip)));

	JobDriver driver{ job };
	driver.autoCancelUnscripted = false; // The queued request event is already answered; its dispatch is not a hang
	REQUIRE(driver.pumpToCompletion());
	CHECK(driver.summary()->status == CompletionStatus::Completed);
	CHECK(driver.summary()->skippedItems == 1);
	CHECK(readFileContents(base % "/dest.bin") == patternedContents(50));
}

TEST_CASE("job: progress after a decision barrier is not merged into earlier snapshots", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src1.bin", patternedContents(500));
	writeTestFile(base % "/src2.bin", patternedContents(300'000));
	REQUIRE(QDir{}.mkpath(base % "/dest"));
	writeTestFile(base % "/dest/src1.bin", patternedContents(10)); // Collides: the prompt is the barrier

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src1.bin", base % "/src2.bin" },
		DestinationIntent::IntoDirectory, base % "/dest"), 1024 };
	JobDriver driver{ job };
	driver.decisions = { act(DecisionAction::Replace) };

	job.start();
	REQUIRE(driver.pumpToCompletion());
	CHECK(driver.summary()->status == CompletionStatus::Completed);
	CHECK(driver.summary()->completedItems == 2);

	size_t requestIndex = driver.events.size();
	for (size_t i = 0; i < driver.events.size(); ++i)
	{
		if (std::holds_alternative<DecisionRequest>(driver.events[i]))
		{
			requestIndex = i;
			break;
		}
	}
	REQUIRE(requestIndex < driver.events.size());

	// The second file's progress must appear as entries after the barrier, never coalesced across it.
	bool progressAfterRequest = false;
	for (size_t i = requestIndex + 1; i < driver.events.size(); ++i)
		progressAfterRequest = progressAfterRequest || std::holds_alternative<ProgressSnapshot>(driver.events[i]);
	CHECK(progressAfterRequest);
}

TEST_CASE("job: progress coalescing and barrier ordering", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	// Two roots in request order: the first streams progress, the second collides and prompts.
	writeTestFile(base % "/first.bin", patternedContents(100'000));
	writeTestFile(base % "/second.bin", patternedContents(300));
	REQUIRE(QDir{}.mkpath(base % "/dest"));
	writeTestFile(base % "/dest/second.bin", patternedContents(40));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/first.bin", base % "/second.bin" },
		DestinationIntent::IntoDirectory, base % "/dest"), 1024 };
	JobDriver driver{ job };
	driver.autoCancelUnscripted = false;

	job.start();
	REQUIRE(waitUntil([&job] { return job.hasPendingDecision(); }));

	// Undrained queue at the prompt: the first file's many snapshots coalesced into one, and the decision
	// barrier was appended after it - progress published before a barrier stays before it.
	job.processEvents(driver);
	REQUIRE(driver.events.size() == 2);
	const auto* coalesced = std::get_if<ProgressSnapshot>(&driver.events[0]);
	REQUIRE(coalesced);
	CHECK(coalesced->bytesProcessed == 100'000); // The latest snapshot won, not an early partial one
	CHECK(std::holds_alternative<DecisionRequest>(driver.events[1]));

	REQUIRE(job.submitDecision(act(DecisionAction::Skip)));
	REQUIRE(driver.pumpToCompletion());

	CHECK(driver.summaryCount() == 1);
	CHECK(driver.decisionRequestCount() == 1);
	CHECK(std::holds_alternative<OperationSummary>(driver.events.back()));
	CHECK(driver.summary()->status == CompletionStatus::Completed);
	CHECK(driver.summary()->completedItems == 1);
	CHECK(driver.summary()->skippedItems == 1);
	CHECK(readFileContents(base % "/dest/first.bin") == patternedContents(100'000));
	CHECK(readFileContents(base % "/dest/second.bin") == patternedContents(40));
}

TEST_CASE("job: cancellation requested before start takes effect at the first checkpoint", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(1000));

	CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
	JobDriver driver{ job };

	job.cancel();
	CHECK(job.status() == JobStatus::NotStarted);

	job.start();
	REQUIRE(driver.pumpToCompletion());
	CHECK(driver.summary()->status == CompletionStatus::Cancelled);
	CHECK(driver.summary()->completedItems == 0);
	CHECK(entryAbsent(base % "/dest.bin")); // The first checkpoint precedes any mutation, so nothing was copied
}

TEST_CASE("job: the destructor is the lifetime barrier", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/big.bin", patternedContents(16 * 1024 * 1024));

	{
		CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/big.bin" }, DestinationIntent::ExactEntry, base % "/copy.bin"), 1024 };
		job.start();
		// 16k chunk writes remain at this point; the destructor's cancellation lands far before they finish.
		REQUIRE(waitUntil([&base] { return stagingFileCount(base) == 1; }));
	}

	// The destructor joined: the worker has fully unwound, which includes aborting and removing its staging file.
	CHECK(stagingFileCount(base) == 0);
	CHECK(entryAbsent(base % "/copy.bin"));
}

TEST_CASE("job: the destructor unblocks a worker waiting on a decision", "[fileoperationjob]")
{
	QTemporaryDir tempDir;
	REQUIRE(tempDir.isValid());
	const QString base = tempDir.path();

	writeTestFile(base % "/src.bin", patternedContents(700));
	writeTestFile(base % "/dest.bin", patternedContents(50));

	{
		CFileOperationJob job{ transferRequest(TransferKind::Copy, { base % "/src.bin" }, DestinationIntent::ExactEntry, base % "/dest.bin"), 1024 };
		job.start();
		// Never pumped, never answered: the worker sits inside the decision wait when the job dies.
		REQUIRE(waitUntil([&job] { return job.hasPendingDecision(); }));
	}

	// The destructor's cancellation woke the decision wait and joined; reaching this line is the hang proof.
	CHECK(readFileContents(base % "/dest.bin") == patternedContents(50)); // The replacement was never authorized
	CHECK(readFileContents(base % "/src.bin") == patternedContents(700));
	CHECK(stagingFileCount(base) == 0);
}

TEST_CASE("job: concurrent independent jobs", "[fileoperationjob]")
{
	QTemporaryDir tempDirA, tempDirB;
	REQUIRE(tempDirA.isValid());
	REQUIRE(tempDirB.isValid());
	const QString baseA = tempDirA.path(), baseB = tempDirB.path();

	REQUIRE(QDir{}.mkpath(baseA % "/src"));
	writeTestFile(baseA % "/src/a.bin", patternedContents(50'000));
	writeTestFile(baseA % "/src/b.bin", patternedContents(60'000));
	REQUIRE(QDir{}.mkpath(baseA % "/dest"));

	REQUIRE(QDir{}.mkpath(baseB % "/victim/sub"));
	writeTestFile(baseB % "/victim/a.bin", patternedContents(1000));
	writeTestFile(baseB % "/victim/sub/b.bin", patternedContents(2000));

	CFileOperationJob copyJob{ transferRequest(TransferKind::Copy, { baseA % "/src" }, DestinationIntent::IntoDirectory, baseA % "/dest"), 1024 };
	CFileOperationJob deleteJob{ deleteRequest({ baseB % "/victim" }) };
	JobDriver copyDriver{ copyJob }, deleteDriver{ deleteJob };

	copyJob.start();
	deleteJob.start();

	REQUIRE(waitUntil([&] {
		copyJob.processEvents(copyDriver);
		deleteJob.processEvents(deleteDriver);
		return copyDriver.summary() != nullptr && deleteDriver.summary() != nullptr;
	}));

	CHECK(copyDriver.summary()->status == CompletionStatus::Completed);
	CHECK(copyDriver.summary()->completedItems == 3);
	requireEqualTrees(baseA % "/src", baseA % "/dest/src");

	CHECK(deleteDriver.summary()->status == CompletionStatus::Completed);
	CHECK(deleteDriver.summary()->completedItems == 4);
	CHECK(entryAbsent(baseB % "/victim"));
}

#include "paneltesthelpers.h"
#include "filesystemwatcher/cdirectorysnapshotpoller.h"

#include <chrono>
#include <thread>

// The poll thread's first scan waits 100 ms; after that, a poll every PollPeriodMs
constexpr unsigned int PollPeriodMs = 20;

// The deadline is far beyond a poll period: it only bounds a test that fails
[[nodiscard]] static bool waitForReportedChange(CDirectorySnapshotPoller& poller)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 5 };
	while (!poller.changesDetected())
	{
		if (std::chrono::steady_clock::now() > deadline)
			return false;

		std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
	}

	return true;
}

// Before checking that nothing was reported: the first scan and several polls run meanwhile
static void waitOutSeveralPolls()
{
	std::this_thread::sleep_for(std::chrono::milliseconds{ 100 + 10 * PollPeriodMs });
}

TEST_CASE("CDirectorySnapshotPoller - a change made after the baseline is reported once", "[watcher][poller]")
{
	TempTree tree;
	CDirectorySnapshotPoller poller{ PollPeriodMs };
	poller.setPathToWatch(tree.path());
	poller.captureBaselineState();

	// Made before the first poll, which would otherwise take it into the baseline
	tree.makeFile(QStringLiteral("new.txt"));
	CHECK(waitForReportedChange(poller));

	// The polls after the report compare against the folder with the new file in it
	waitOutSeveralPolls();
	CHECK_FALSE(poller.changesDetected());
}

TEST_CASE("CDirectorySnapshotPoller - an unchanged folder reports nothing", "[watcher][poller]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("sub"));
	tree.makeFile(QStringLiteral("file.txt"), "contents");

	CDirectorySnapshotPoller poller{ PollPeriodMs };
	poller.setPathToWatch(tree.path());
	// Taken on this thread; the polls scan on the poll thread
	poller.captureBaselineState();

	waitOutSeveralPolls();
	CHECK_FALSE(poller.changesDetected());
}

TEST_CASE("CDirectorySnapshotPoller - deleting the empty folder being watched is reported", "[watcher][poller]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	CDirectorySnapshotPoller poller{ PollPeriodMs };
	poller.setPathToWatch(sub);
	poller.captureBaselineState();

	REQUIRE(QDir{}.rmdir(sub));
	CHECK(waitForReportedChange(poller));
}

TEST_CASE("CDirectorySnapshotPoller - a change in the folder left behind is not reported for the next one", "[watcher][poller]")
{
	TempTree tree;
	const QString left = tree.makeDir(QStringLiteral("left"));
	const QString entered = tree.makeDir(QStringLiteral("entered"));

	CDirectorySnapshotPoller poller{ PollPeriodMs };
	poller.setPathToWatch(left);
	poller.captureBaselineState();
	tree.makeFile(QStringLiteral("left/new.txt"));

	// The poll thread may be scanning the folder left behind, or have already reported the change in it
	poller.setPathToWatch(entered);
	poller.captureBaselineState();
	waitOutSeveralPolls();
	CHECK_FALSE(poller.changesDetected());

	tree.makeFile(QStringLiteral("entered/new.txt"));
	CHECK(waitForReportedChange(poller));
}

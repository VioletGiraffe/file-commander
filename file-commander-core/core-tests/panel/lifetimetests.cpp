#include "paneltesthelpers.h"

TEST_CASE("CPanel - destroying a panel retires the listing it had queued", "[panel][lifetime]")
{
	TempTree tree;

	CThreadPool pool{ 1, "Panel test pool" };
	WorkerGate gate{ pool };
	RecordingListener listener;

	gate.close();
	{
		CPanel panel{ Panel::LeftPanel, pool, 7 };
		panel.addPanelContentsChangedListener(&listener);
		REQUIRE(panel.setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	} // Retires the queued listing rather than waiting for the parked worker to get to it

	gate.open();
	gate.waitUntilWorkComplete();

	// Nothing ran against the destroyed panel, and its undelivered notifications died with its queue.
	CHECK(listener.count(PanelEvent::ContentsChanged) == 0);
	CHECK(listener.count(PanelEvent::ContentsInvalidated) == 0);
}

TEST_CASE("CPanel - retiring one panel leaves another panel's work alone", "[panel][lifetime]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	CThreadPool pool{ 1, "Panel test pool" };
	WorkerGate gate{ pool };
	RecordingListener listener;
	CPanel survivor{ Panel::RightPanel, pool, 2 };
	survivor.addPanelContentsChangedListener(&listener);

	gate.close();
	{
		CPanel doomed{ Panel::LeftPanel, pool, 1 };
		REQUIRE(doomed.setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
		REQUIRE(survivor.setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	}

	gate.open();
	gate.waitUntilWorkComplete();
	survivor.uiThreadTimerTick();

	CHECK(listener.count(PanelEvent::ContentsChanged) == 1);
	CHECK(survivor.itemHashExists(hashOf(sub)));
}

TEST_CASE("CPanel - reactivating a panel picks up what changed while it was inactive", "[panel][watcher]")
{
	TempTree tree;
	tree.makeFile(QStringLiteral("before.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	// An inactive tab gives up its watch handle, so nothing detects this one.
	h.panel().setActive(false);
	const QString added = tree.makeFile(QStringLiteral("while-inactive.txt"));
	h.listener().clear();

	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().itemHashExists(hashOf(added)));
	CHECK(h.listener().count(PanelEvent::ContentsChanged) >= 1);
}

TEST_CASE("CPanel - a change on disk refreshes the folder in view", "[panel][watcher]")
{
	TempTree tree;
	tree.makeFile(QStringLiteral("before.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	const QString added = tree.makeFile(QStringLiteral("after.txt"));

	// The only case in this suite that waits rather than steps: the watcher is a native change handle on Windows
	// and a polling thread elsewhere, so when it reports is not ours to decide. Waiting on the notification rather
	// than on the contents also orders the two - the listing is committed before it is announced.
	CHECK(h.pumpUntil([&h] { return h.listener().count(PanelEvent::ContentsChanged) >= 1; }));
	CHECK(h.panel().itemHashExists(hashOf(added)));
}

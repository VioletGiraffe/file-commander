#include "paneltesthelpers.h"

TEST_CASE("CPanel - a listing that outlives the drain blanks the view first", "[panel][notifications]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));
	const QString inner = tree.makeFile(QStringLiteral("sub/inner.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	h.worker().close();
	REQUIRE(h.panel().setPath(sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.tick();

	// The path change is reported as it happens; the contents can only be reported as gone.
	const std::vector<PanelEvent::Type> expectedSequence{ PanelEvent::CurrentPathChanged, PanelEvent::ContentsInvalidated };
	CHECK(h.listener().sequence() == expectedSequence);
	const PanelEvent& invalidated = h.listener().only(PanelEvent::ContentsInvalidated);
	CHECK(invalidated.tabId == h.panel().id());

	// The committed list describes the folder we left, so the accessors stop returning it the moment we leave.
	CHECK(h.panel().list().empty());
	CHECK(h.panel().itemHashes().empty());
	CHECK_FALSE(h.panel().itemHashExists(hashOf(sub)));

	h.worker().open();
	h.completeUpdate();

	const PanelEvent& contentsChanged = h.listener().only(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseForwardNavigation);
	CHECK(h.panel().itemHashExists(hashOf(inner)));
}

TEST_CASE("CPanel - a listing that completes within the drain never blanks the view", "[panel][notifications]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	REQUIRE(h.panel().setPath(sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	// The listing finishes before the drain, so its notification replaces the pending invalidation - both carry
	// the same queue tag, and enqueuing one erases the other.
	h.completeUpdate();

	CHECK(h.listener().count(PanelEvent::ContentsInvalidated) == 0);
	const PanelEvent& contentsChanged = h.listener().only(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseForwardNavigation);
}

TEST_CASE("CPanel - a superseded file list update is discarded", "[panel][notifications]")
{
	TempTree tree;
	const QString first = tree.makeDir(QStringLiteral("first"));
	const QString firstFile = tree.makeFile(QStringLiteral("first/a.txt"));
	const QString second = tree.makeDir(QStringLiteral("second"));
	const QString secondFile = tree.makeFile(QStringLiteral("second/b.txt"));

	PanelHarness h;
	h.worker().close();
	REQUIRE(h.panel().setPath(first, refreshCauseOther) == FileOperationResultCode::Ok);
	h.tick();
	REQUIRE(h.panel().setPath(second, refreshCauseOther) == FileOperationResultCode::Ok);
	h.worker().open();
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == second + '/');
	CHECK(h.panel().itemHashExists(hashOf(secondFile)));
	CHECK_FALSE(h.panel().itemHashExists(hashOf(firstFile)));
	// The first folder's listing was built and then dropped on the way in: only one of the two reports contents.
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 1);
}

TEST_CASE("CPanel - refreshing the folder in view does not blank it", "[panel][notifications]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	h.worker().close();
	h.panel().refreshFileList(refreshCauseOther);
	h.tick();

	CHECK(h.listener().count(PanelEvent::ContentsInvalidated) == 0);
	// The committed list still belongs to the folder in view, so it stays readable throughout the update.
	CHECK(h.panel().itemHashExists(hashOf(file)));

	h.worker().open();
	h.completeUpdate();

	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 1);
}

TEST_CASE("CPanel - flattened mode lists the files of the whole subtree", "[panel][notifications]")
{
	TempTree tree;
	const QString nested = tree.makeDir(QStringLiteral("a/b"));
	const QString top = tree.makeFile(QStringLiteral("top.txt"));
	const QString deep = tree.makeFile(QStringLiteral("a/b/deep.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	h.worker().close();
	h.panel().showAllFilesFromCurrentFolderAndBelow();
	h.tick();

	// Same folder, but a different view of it: what was committed no longer describes what is on screen.
	CHECK(h.listener().count(PanelEvent::ContentsInvalidated) == 1);

	h.worker().open();
	h.completeUpdate();

	CHECK(h.panel().itemHashExists(hashOf(top)));
	CHECK(h.panel().itemHashExists(hashOf(deep)));
	// Files only, and no [..] row.
	CHECK_FALSE(h.panel().itemHashExists(hashOf(nested)));
	CHECK(h.panel().itemHashes().size() == 2);

	h.panel().navigateUp();
	h.settle();

	// Leaving flattened mode returns to the same folder, listed normally.
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().itemHashExists(hashOf(tree.path() + QStringLiteral("/a"))));
}

TEST_CASE("CPanel - a drain only ever delivers the notifications of its own tab", "[panel][notifications]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	// Declared first so it outlives both panels that hold a pointer to it.
	RecordingListener shared;
	PanelHarness h;
	// A listener is attached to every tab of a side, so the only thing that tells the notifications apart is the
	// id they carry.
	CPanel other{ Panel::LeftPanel, h.pool(), 2 };
	h.panel().addPanelContentsChangedListener(&shared);
	other.addPanelContentsChangedListener(&shared);

	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	REQUIRE(other.setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.worker().waitUntilWorkComplete();

	h.tick();
	REQUIRE_FALSE(shared.events().empty());
	for (const PanelEvent& event : shared.events())
		CHECK(event.tabId == h.panel().id());

	shared.clear();
	other.uiThreadTimerTick();
	REQUIRE_FALSE(shared.events().empty());
	for (const PanelEvent& event : shared.events())
		CHECK(event.tabId == other.id());
}

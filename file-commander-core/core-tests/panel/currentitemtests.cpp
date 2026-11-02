#include "paneltesthelpers.h"

TEST_CASE("CPanel - entering a folder makes it the current item of the one left behind", "[panel][currentitem]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	REQUIRE(h.panel().setPath(sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(sub));
	// Recorded without a notification: the refresh setPath starts reads it back to place the cursor.
	CHECK(h.listener().count(PanelEvent::CurrentItemChanged) == 0);
}

TEST_CASE("CPanel - stepping out of a folder makes it the current item of the parent", "[panel][currentitem]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	REQUIRE(h.panel().setPath(tree.path(), refreshCauseCdUp) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(sub));
	CHECK(h.listener().count(PanelEvent::CurrentItemChanged) == 0);
	// The remembered hash is the one the folder we came from has in the new listing - that identity is what
	// puts the cursor back on it.
	CHECK(h.panel().itemHashExists(hashOf(sub)));
}

TEST_CASE("CPanel - the remembered current item survives a listing that has not arrived yet", "[panel][currentitem]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	h.worker().close();
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseCdUp) == FileOperationResultCode::Ok);
	h.tick();

	// The view is blank at this point, and nothing derived from the contents may be acted on until they arrive -
	// least of all by overwriting what the pending navigation just recorded.
	CHECK(h.listener().count(PanelEvent::ContentsInvalidated) == 1);
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(sub));

	h.worker().open();
	h.completeUpdate();

	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(sub));
}

TEST_CASE("CPanel - entering a folder records nothing when the folder left behind was never listed", "[panel][currentitem]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	h.worker().close();
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	// Straight on into the subfolder, with the parent's listing still unbuilt.
	REQUIRE(h.panel().setPath(sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.worker().open();
	h.settle();

	// There was no committed list to find the subfolder in, so there was nothing to record.
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == 0);
}

TEST_CASE("CPanel - setCurrentItemHashForFolder notifies through the queue, never inline", "[panel][currentitem]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	h.panel().setCurrentItemHashForFolder(tree.path(), hashOf(file));
	// Inline delivery would run listeners inside the caller's stack. Inline rename gets here from within the
	// delegate's commitData, where the view is still editing and refuses to move the cursor.
	CHECK(h.listener().count(PanelEvent::CurrentItemChanged) == 0);

	h.tick();

	const PanelEvent& event = h.listener().only(PanelEvent::CurrentItemChanged);
	CHECK(event.tabId == h.panel().id());
	CHECK(event.path == tree.path());
	CHECK(event.itemHash == hashOf(file));
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(file));
}

TEST_CASE("CPanel - a folder is one key in the current item map however it is spelled", "[panel][currentitem]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));
	const QString otherFile = tree.makeFile(QStringLiteral("b.txt"));

	PanelHarness h;
	h.panel().setCurrentItemHashForFolder(tree.path(), hashOf(file), false);
	CHECK(h.panel().currentItemHashForFolder(tree.path() + '/') == hashOf(file));

	// The slashed spelling addresses that same entry rather than adding a second one.
	h.panel().setCurrentItemHashForFolder(tree.path() + '/', hashOf(otherFile), false);
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(otherFile));
}

TEST_CASE("CPanel - a folder nothing was ever recorded for has no current item", "[panel][currentitem]")
{
	TempTree tree;

	PanelHarness h;
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == 0);
}

TEST_CASE("CPanel - goToItem opens the containing folder with the item as its current one", "[panel][currentitem]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));
	const QString file = tree.makeFile(QStringLiteral("sub/target.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	CHECK(h.panel().goToItem(CFileSystemObject{ file }));
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == sub + '/');
	CHECK(h.panel().currentItemHashForFolder(sub) == hashOf(file));
	CHECK(h.panel().itemHashExists(hashOf(file)));
	// Notifying from goToItem would reach the panel before the refresh, against the folder being left.
	CHECK(h.listener().count(PanelEvent::CurrentItemChanged) == 0);
}

TEST_CASE("CPanel - goToItem on something that is gone changes nothing", "[panel][currentitem]")
{
	TempTree tree;
	const QString missing = tree.path(QStringLiteral("sub/gone.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	CHECK_FALSE(h.panel().goToItem(CFileSystemObject{ missing }));

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().currentItemHashForFolder(tree.path(QStringLiteral("sub"))) == 0);
	CHECK(h.listener().events().empty());
}

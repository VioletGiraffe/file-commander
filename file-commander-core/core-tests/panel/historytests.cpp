#include "paneltesthelpers.h"

TEST_CASE("CPanel - back and forward walk the folders that were visited", "[panel][history]")
{
	TempTree tree;
	const QString a = tree.makeDir(QStringLiteral("a"));
	const QString b = tree.makeDir(QStringLiteral("b"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	REQUIRE(h.panel().setPath(a, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.settle();
	REQUIRE(h.panel().setPath(b, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().navigateBack());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == a + '/');

	CHECK(h.panel().navigateBack());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');

	CHECK(h.panel().navigateForward());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == a + '/');

	CHECK(h.panel().navigateForward());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == b + '/');

	// Nowhere further to go, and nothing happens as a result.
	CHECK_FALSE(h.panel().navigateForward());
	CHECK(h.panel().currentDirPathPosix() == b + '/');
}

TEST_CASE("CPanel - going back skips a folder that no longer exists", "[panel][history]")
{
	TempTree tree;
	const QString a = tree.makeDir(QStringLiteral("a"));
	const QString b = tree.makeDir(QStringLiteral("b"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	REQUIRE(h.panel().setPath(a, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.settle();
	REQUIRE(h.panel().setPath(b, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	REQUIRE(QDir{ a }.removeRecursively());

	CHECK(h.panel().navigateBack());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
}

TEST_CASE("CPanel - navigating up records the folder left behind", "[panel][history]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	h.panel().navigateUp();
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().currentItemHashForFolder(tree.path()) == hashOf(sub));
	const PanelEvent& contentsChanged = h.listener().last(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseCdUp);
}

TEST_CASE("CPanel - leaving flattened mode returns to the same folder", "[panel][history]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));
	tree.makeFile(QStringLiteral("sub/deep.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	h.panel().showAllFilesFromCurrentFolderAndBelow();
	h.settle();
	REQUIRE_FALSE(h.panel().itemHashExists(hashOf(sub)));

	// In flattened mode, back is not a history move either - it is the way out of the flattened view.
	CHECK(h.panel().navigateBack());
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().itemHashExists(hashOf(sub)));
}

TEST_CASE("CPanel - restored history is navigable", "[panel][history]")
{
	TempTree tree;
	const QString a = tree.makeDir(QStringLiteral("a"));
	const QString b = tree.makeDir(QStringLiteral("b"));

	PanelHarness h;
	h.panel().restoreHistory({ a + '/', b + '/' });
	REQUIRE(h.panel().history().size() == 2);

	REQUIRE(h.panel().setPath(b, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	// Already the latest entry, so nothing is appended to it.
	CHECK(h.panel().history().size() == 2);

	CHECK(h.panel().navigateBack());
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == a + '/');
}

TEST_CASE("CPanel - the path-changed notification tracks the folder, not the history cursor", "[panel][history]")
{
	TempTree tree;
	const QString a = tree.makeDir(QStringLiteral("a"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	REQUIRE(h.panel().setPath(a, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	// Going back moves the history cursor instead of appending to it, but the folder in view still changed.
	CHECK(h.panel().navigateBack());
	h.settle();

	const PanelEvent& pathChanged = h.listener().only(PanelEvent::CurrentPathChanged);
	CHECK(pathChanged.path == tree.path() + '/');
}

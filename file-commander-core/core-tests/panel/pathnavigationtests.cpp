#include "paneltesthelpers.h"

#ifdef _WIN32
// The only way to make every candidate in a path hierarchy inaccessible: elsewhere the hierarchy ends at "/",
// which always exists.
[[nodiscard]] static QString unusedDriveRoot()
{
	for (char letter = 'Z'; letter >= 'D'; --letter)
	{
		const QString root = QString{ QChar::fromLatin1(letter) } + QStringLiteral(":/");
		if (!QDir{ root }.exists())
			return root;
	}

	return {};
}
#endif

TEST_CASE("CPanel - setPath lists an existing folder", "[panel][path]")
{
	TempTree tree;
	const QString subDir = tree.makeDir(QStringLiteral("sub"));
	const QString file = tree.makeFile(QStringLiteral("file.txt"), "contents");

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().itemHashExists(hashOf(subDir)));
	CHECK(h.panel().itemHashExists(hashOf(file)));
	CHECK(h.panel().itemPathByHash(hashOf(file)) == file);
	// A directory's path always ends with a separator, which is what makes its hash independent of the spelling.
	CHECK(h.panel().itemPathByHash(hashOf(subDir)) == subDir + '/');
	// The two entries plus the [..] row, which QDir::NoDot keeps.
	CHECK(h.panel().itemHashes().size() == 3);
}

TEST_CASE("CPanel - setPath to a file lands on its parent folder", "[panel][path]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("file.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(file, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
}

TEST_CASE("CPanel - setPath to a path that does not exist lands on the closest folder that does", "[panel][path]")
{
	TempTree tree;

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path() + QStringLiteral("/no/such/place"), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
}

TEST_CASE("CPanel - a wholly inaccessible path leaves the panel where it was", "[panel][path]")
{
#ifdef _WIN32
	const QString unusableRoot = unusedDriveRoot();
	if (unusableRoot.isEmpty())
	{
		WARN("Every drive letter is in use, skipping");
		return;
	}

	TempTree tree;
	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	CHECK(h.panel().setPath(unusableRoot + QStringLiteral("nowhere"), refreshCauseCdUp) == FileOperationResultCode::DirNotAccessible);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().history().currentItem() == tree.path() + '/');
	// The requested cause is not reported: the navigation it described did not happen.
	const PanelEvent& contentsChanged = h.listener().last(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseOther);
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 0);
#else
	WARN("Every path hierarchy ends at an accessible root on this platform, skipping");
#endif
}

TEST_CASE("CPanel - a folder that vanishes before its listing runs falls back to the parent", "[panel][path]")
{
	TempTree tree;
	const QString doomed = tree.makeDir(QStringLiteral("doomed"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(doomed, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	// Enter the folder, then remove it before its listing gets to run. The accessibility check the worker makes is
	// not redundant with the one setPath already made - this window is the whole reason it exists.
	h.worker().close();
	REQUIRE(h.panel().setPath(doomed, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	REQUIRE(QDir{ doomed }.removeRecursively());
	h.worker().open();

	// Recovery re-enters setPath from inside the tick, which starts another update; settle() runs both rounds.
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	// Into the folder, then back out of it once it turned out to be gone.
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 2);
	// Landing on the parent is not the navigation that was asked for, so it is not reported as one.
	const PanelEvent& contentsChanged = h.listener().last(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseOther);
}

TEST_CASE("CPanel - a trailing separator does not make a folder a different location", "[panel][path]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	const size_t historySize = h.panel().history().size();
	h.listener().clear();

	REQUIRE(h.panel().setPath(sub + '/', refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == sub + '/');
	CHECK(h.panel().history().size() == historySize);
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 0);
}

TEST_CASE("CPanel - navigating up from the root only refreshes", "[panel][path]")
{
	PanelHarness h;
	const QString root = QDir::rootPath();
	REQUIRE(h.panel().setPath(root, refreshCauseOther) == FileOperationResultCode::Ok);
	h.settle();
	h.listener().clear();

	// There is no parent to go to, so this notifies directly instead of starting an update - one drain delivers it.
	h.panel().navigateUp();
	h.tick();

	CHECK(h.panel().currentDirPathPosix() == root);
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 0);
	// Nothing was left behind, so there is nothing to stop displaying either.
	CHECK(h.listener().count(PanelEvent::ContentsInvalidated) == 0);
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 1);
}

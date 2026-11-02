#include "paneltesthelpers.h"
#include "panellisting.h"

#include <atomic>
#include <vector>

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
	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.panel().itemHashExists(hashOf(subDir)));
	CHECK(h.panel().itemHashExists(hashOf(file)));
	CHECK(h.panel().itemByHash(hashOf(file)).fullAbsolutePath() == file);
	// A directory's path always ends with a separator, which is what makes its hash independent of the spelling.
	CHECK(h.panel().itemByHash(hashOf(subDir)).fullAbsolutePath() == subDir + '/');
	// The two entries plus the [..] row, which QDir::NoDot keeps.
	CHECK(committedItemCount(h.panel()) == 3);
}

TEST_CASE("CPanel - setPath to a file lands on its parent folder", "[panel][path]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("file.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(file, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
}

TEST_CASE("CPanel - setPath to a path that does not exist lands on the closest folder that does", "[panel][path]")
{
	TempTree tree;
	const QString missing = tree.path() + QStringLiteral("/no/such/place");

	PanelHarness h;
	REQUIRE(h.panel().setPath(missing, refreshCauseOther) == FileOperationResultCode::Ok);
	// An inactive panel keeps the path it was given until it is activated: a background tab keeps a folder that is
	// only unreachable for now, such as one on a share that is asleep at startup.
	CHECK(h.panel().currentDirPathPosix() == missing + '/');

	h.panel().setActive(true);
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
	h.panel().setActive(true);
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

TEST_CASE("CPanel - an inaccessible path with nowhere to fall back to lands on the home folder", "[panel][path]")
{
#ifdef _WIN32
	const QString unusableRoot = unusedDriveRoot();
	if (unusableRoot.isEmpty())
	{
		WARN("Every drive letter is in use, skipping");
		return;
	}

	// A restored tab starts with no history and nowhere it came from, which is the state a saved path on a drive
	// that is no longer connected is resolved against.
	PanelHarness h;
	REQUIRE(h.panel().setPath(unusableRoot + QStringLiteral("nowhere"), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == QDir::homePath() + '/');
#else
	WARN("Every path hierarchy ends at an accessible root on this platform, skipping");
#endif
}

TEST_CASE("CPanel - a folder that vanishes before its listing runs falls back to the parent", "[panel][path]")
{
	TempTree tree;
	const QString doomed = tree.makeDir(QStringLiteral("doomed"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	// Enter the folder, then remove it before its listing gets to run. The accessibility check the worker makes is
	// not redundant with the one setPath already made - this window is the whole reason it exists.
	h.worker().close();
	REQUIRE(h.panel().setPath(doomed, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	REQUIRE(QDir{ doomed }.removeRecursively());
	h.worker().open();

	// Recovery moves the panel from inside the tick, which starts another update; settle() runs both rounds.
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	// Into the folder, then back out of it once it turned out to be gone.
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 2);
	// Landing on the parent is not the navigation that was asked for, so it is not reported as one.
	const PanelEvent& contentsChanged = h.listener().last(PanelEvent::ContentsChanged);
	CHECK(contentsChanged.cause == refreshCauseOther);
}

TEST_CASE("CPanel - refreshing a folder that has vanished falls back to the parent", "[panel][path]")
{
	TempTree tree;
	const QString doomed = tree.makeDir(QStringLiteral("doomed"));
	tree.makeFile(QStringLiteral("doomed/a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(doomed, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	REQUIRE(h.panel().currentDirPathPosix() == doomed + '/');
	h.listener().clear();

	REQUIRE(QDir{ doomed }.removeRecursively());
	h.panel().refreshFileList(refreshCauseOther);
	h.settle();

	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(h.listener().count(PanelEvent::CurrentPathChanged) == 1);
	CHECK(h.listener().last(PanelEvent::ContentsChanged).cause == refreshCauseOther);
}

TEST_CASE("CPanel - flattened mode over a tree with no files lists nothing and stays in the folder", "[panel][path]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("a/b"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.listener().clear();

	h.panel().showAllFilesFromCurrentFolderAndBelow();
	h.settle();

	// An empty result is also what a folder that cannot be listed yields; recovery would relist it in the normal mode.
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(committedItemCount(h.panel()) == 0);
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 1);
}

TEST_CASE("CPanel - a trailing separator does not make a folder a different location", "[panel][path]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(sub, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
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
	h.panel().setActive(true);
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

// Records every folder checked; folders under unlistableFolder (which ends with a separator) cannot be listed
[[nodiscard]] static std::function<bool(const QString&)> recordingFolderCheck(std::vector<QString>& checked, const QString& unlistableFolder)
{
	return [&checked, unlistableFolder](const QString& folder) {
		checked.push_back(folder);
		const QString folderWithSeparator = folder.endsWith('/') ? folder : folder + '/';
		return !folderWithSeparator.startsWith(unlistableFolder);
	};
}

TEST_CASE("ListableFolderProbe - folders under an unreachable one are skipped without a check", "[panel][path]")
{
	const QString root = QDir::rootPath();
	const QString unreachable = QString{ root % QStringLiteral("mnt/share/") };
	std::vector<QString> checked;
	const std::atomic<bool> cancelled{ false };
	ListableFolderProbe probe{ cancelled, recordingFolderCheck(checked, unreachable) };

	const QString deepFolder = QString{ unreachable % QStringLiteral("a/b/") };
	CHECK_FALSE(probe.isListable(deepFolder));
	// The folder, then its ancestors from the root down, stopping at the first one that cannot be listed
	CHECK(checked == std::vector<QString>{ deepFolder, root, QString{ root % QStringLiteral("mnt/") }, unreachable });

	checked.clear();
	CHECK_FALSE(probe.isListable(QString{ unreachable % QStringLiteral("c") }));
	CHECK_FALSE(probe.isListable(unreachable));
	CHECK(checked.empty());

	// A sibling of the unreachable folder is checked as usual
	CHECK(probe.isListable(QString{ root % QStringLiteral("mnt/other/") }));
	CHECK(checked.size() == 1);
}

TEST_CASE("ListableFolderProbe - a folder that is gone from a reachable volume is skipped along with its subfolders", "[panel][path]")
{
	const QString root = QDir::rootPath();
	const QString gone = QString{ root % QStringLiteral("data/gone/") };
	std::vector<QString> checked;
	const std::atomic<bool> cancelled{ false };
	ListableFolderProbe probe{ cancelled, recordingFolderCheck(checked, gone) };

	CHECK_FALSE(probe.isListable(gone));
	// Every ancestor answered, so the folder itself is the one skipped from now on
	CHECK(checked.size() == 3);

	checked.clear();
	CHECK_FALSE(probe.isListable(QString{ gone % QStringLiteral("sub/") }));
	CHECK(checked.empty());
	CHECK(probe.isListable(QString{ root % QStringLiteral("data/") }));
}

TEST_CASE("ListableFolderProbe - a cancelled probe checks nothing", "[panel][path]")
{
	std::vector<QString> checked;
	const std::atomic<bool> cancelled{ true };
	ListableFolderProbe probe{ cancelled, recordingFolderCheck(checked, {}) };

	CHECK_FALSE(probe.isListable(QDir::rootPath()));
	CHECK(checked.empty());
}

TEST_CASE("ListableFolderProbe - a folder whose listing failed is recorded without checking it again", "[panel][path]")
{
	const QString root = QDir::rootPath();
	const QString unreachable = QString{ root % QStringLiteral("mnt/share/") };
	std::vector<QString> checked;
	const std::atomic<bool> cancelled{ false };
	ListableFolderProbe probe{ cancelled, recordingFolderCheck(checked, unreachable) };

	const QString failedFolder = QString{ unreachable % QStringLiteral("a/") };
	probe.addUnlistable(failedFolder);
	// Only its ancestors, from the root down to the first one that cannot be listed
	CHECK(checked == std::vector<QString>{ root, QString{ root % QStringLiteral("mnt/") }, unreachable });

	checked.clear();
	CHECK_FALSE(probe.isListable(failedFolder));
	CHECK_FALSE(probe.isListable(unreachable));
	CHECK(checked.empty());
}

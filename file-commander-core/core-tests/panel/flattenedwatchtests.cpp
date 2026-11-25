#include "paneltesthelpers.h"
#include "panellisting.h"
#include "filelisttestitems.h"

#include "link_helpers.hpp" // test-utils

TEST_CASE("removeItemsUnderFolders removes the items any of the folders holds except those kept", "[panel][watcher]")
{
	FileListHashMap items;
	for (const QString& path : { QStringLiteral("/r/a/1"), QStringLiteral("/r/a/b/2"), QStringLiteral("/r/ab/3"), QStringLiteral("/r/c/4"),
		QStringLiteral("/r/c/d/5"), QStringLiteral("/r/6") })
	{
		CFileSystemObject item = makeItemAt(File, path);
		items.emplace(item.hash(), std::move(item));
	}

	FileListHashMap keep;
	CFileSystemObject kept = makeItemAt(File, QStringLiteral("/r/c/d/5"));
	keep.emplace(kept.hash(), kept);

	// Nested and unsorted: the outer folder covers the inner one
	const std::vector<qulonglong> removed = removeItemsUnderFolders(items, { QStringLiteral("/r/c/"), QStringLiteral("/r/a/b/"), QStringLiteral("/r/a/") }, keep);

	CHECK(removed.size() == 3);
	std::vector<QString> remaining;
	for (const auto& [hash, item] : items)
		remaining.push_back(item.fullAbsolutePath());
	std::ranges::sort(remaining);
	CHECK(remaining == std::vector<QString>{ QStringLiteral("/r/6"), QStringLiteral("/r/ab/3"), QStringLiteral("/r/c/d/5") });
}

// The paths of the committed listing, sorted
[[nodiscard]] static std::vector<QString> committedPaths(const CPanel& panel)
{
	std::vector<QString> paths;
	panel.readCommittedContents([&paths](const QString&, const FileListHashMap& contents) {
		for (const auto& [hash, item] : contents)
			paths.push_back(item.fullAbsolutePath());
	});
	std::ranges::sort(paths);
	return paths;
}

// The paths a new flattened listing of root holds, sorted
[[nodiscard]] static std::vector<QString> freshFlattenedListingPaths(const QString& root, bool showHiddenFiles = true)
{
	FileListHashMap items;
	const std::atomic<bool> abort{ false };
	addFlattenedListingItems(root, showHiddenFiles, items, abort);

	std::vector<QString> paths;
	for (const auto& [hash, item] : items)
		paths.push_back(item.fullAbsolutePath());
	std::ranges::sort(paths);
	return paths;
}

// Ticks until the committed listing holds what a new listing of root would
[[nodiscard]] static bool committedListingMatchesDisk(PanelHarness& h, const QString& root, bool showHiddenFiles = true)
{
	return h.pumpUntil([&] { return committedPaths(h.panel()) == freshFlattenedListingPaths(root, showHiddenFiles); });
}

[[nodiscard]] static bool everyContentsChangeWasInPlace(const RecordingListener& listener)
{
	return std::ranges::none_of(listener.events(), [](const PanelEvent& event) { return event.type == PanelEvent::ContentsChanged && event.change.full; });
}

// Shows root flattened, with the listing committed and the listener cleared
static void showFlattened(PanelHarness& h, const QString& root)
{
	REQUIRE(h.panel().setPath(root, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	h.panel().showAllFilesFromCurrentFolderAndBelow();
	h.settle();
	REQUIRE(h.panel().displayMode() == CPanel::AllObjectsMode);
	h.listener().clear();
}

TEST_CASE("CPanel - a flattened view applies the file changes in its tree in place", "[panel][watcher]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("a/b"));
	const QString top = tree.makeFile(QStringLiteral("top.txt"));
	const QString deep = tree.makeFile(QStringLiteral("a/b/deep.txt"));

	PanelHarness h;
	showFlattened(h, tree.path());
	CHECK(h.panel().watchKeepsViewCurrent());

	const QString added = tree.makeFile(QStringLiteral("a/b/added.txt"));
	CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(added)); }));

	tree.makeFile(QStringLiteral("a/b/deep.txt"), QByteArrayLiteral("grown"));
	CHECK(h.pumpUntil([&] { return h.panel().itemByHash(hashOf(deep)).size() == 5; }));

	REQUIRE(QFile::remove(top));
	CHECK(h.pumpUntil([&] { return !h.panel().itemHashExists(hashOf(top)); }));

	const QString moved = tree.path(QStringLiteral("a/moved.txt"));
	REQUIRE(QFile::rename(added, moved));
	CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(moved)) && !h.panel().itemHashExists(hashOf(added)); }));

	CHECK(committedListingMatchesDisk(h, tree.path()));

	REQUIRE(QDir{}.rename(moved, tree.path(QStringLiteral("a/Moved.txt"))));
#ifdef __linux__
	// A case-insensitive volume keeps the old spelling listed: Linux reads a path back in the case asked for (see get_directory_entry)
	if (!QFileInfo::exists(moved))
#endif
		CHECK(committedListingMatchesDisk(h, tree.path()));

	CHECK(h.listener().count(PanelEvent::ContentsChanged) > 0);
	CHECK(everyContentsChangeWasInPlace(h.listener()));
}

TEST_CASE("CPanel - a folder entering or leaving a flattened view brings or takes its files", "[panel][watcher]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("a/b"));
	tree.makeFile(QStringLiteral("a/b/deep.txt"));
	tree.makeFile(QStringLiteral("top.txt"));

	TempTree outside;
	outside.makeDir(QStringLiteral("x/y"));
	outside.makeFile(QStringLiteral("x/one.txt"));
	outside.makeFile(QStringLiteral("x/y/two.txt"));

	PanelHarness h;
	showFlattened(h, tree.path());

	SECTION("Moved in, renamed, then moved out")
	{
		REQUIRE(QDir{}.rename(outside.path(QStringLiteral("x")), tree.path(QStringLiteral("x"))));
		CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(tree.path(QStringLiteral("x/y/two.txt")))); }));
		CHECK(committedListingMatchesDisk(h, tree.path()));

		REQUIRE(QDir{}.rename(tree.path(QStringLiteral("x")), tree.path(QStringLiteral("z"))));
		CHECK(committedListingMatchesDisk(h, tree.path()));
		CHECK(h.panel().itemHashExists(hashOf(tree.path(QStringLiteral("z/one.txt")))));

		REQUIRE(QDir{}.rename(tree.path(QStringLiteral("z")), outside.path(QStringLiteral("x"))));
		CHECK(committedListingMatchesDisk(h, tree.path()));
		CHECK(committedItemCount(h.panel()) == 2);
	}

	SECTION("Deleted")
	{
		REQUIRE(QDir{ tree.path(QStringLiteral("a")) }.removeRecursively());
		CHECK(committedListingMatchesDisk(h, tree.path()));
		CHECK(committedItemCount(h.panel()) == 1);
	}

	CHECK(everyContentsChangeWasInPlace(h.listener()));
}

TEST_CASE("CPanel - a flattened view leaves out a hidden file that appears while hidden files are not shown", "[panel][watcher]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("a"));
	tree.makeFile(QStringLiteral("a/visible.txt"));

	const ShowHiddenFilesSetting hiddenFilesHidden{ false };
	PanelHarness h;
	showFlattened(h, tree.path());

	const QString hidden = tree.makeFile(QStringLiteral("a/.hidden"));
	REQUIRE(setFileHidden(hidden));
	const QString added = tree.makeFile(QStringLiteral("a/added.txt"));

	CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(added)) && !h.panel().itemHashExists(hashOf(hidden)); }));
	CHECK(committedListingMatchesDisk(h, tree.path(), false));
}

TEST_CASE("CPanel - a flattened view follows the folder links that appear in it", "[panel][watcher]")
{
	TempTree tree;
	const QString parent = tree.makeDir(QStringLiteral("a/b"));
	tree.makeFile(QStringLiteral("a/top.txt"));
	tree.makeFile(QStringLiteral("a/b/deep.txt"));
	TempTree outside;
	const QString target = outside.makeDir(QStringLiteral("target"));
	outside.makeFile(QStringLiteral("target/linked.txt"));
	if (filesystemHasNoLinks(tree.path()))
	{
		WARN("This filesystem has no links: test skipped");
		return;
	}

	PanelHarness h;
	showFlattened(h, tree.path());
	REQUIRE(h.panel().watchKeepsViewCurrent());

	// A link to an ancestor: a listing refuses to follow it, a scan starting at the link would not
	REQUIRE(createDirectoryLink(tree.path(QStringLiteral("a")), parent + QStringLiteral("/up")));
	REQUIRE(createDirectoryLink(target, parent + QStringLiteral("/side")));
	CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(parent + QStringLiteral("/side/linked.txt"))); }));
	CHECK(committedListingMatchesDisk(h, tree.path()));

	outside.makeFile(QStringLiteral("target/added.txt"));
	CHECK(h.pumpUntil([&] { return h.panel().itemHashExists(hashOf(parent + QStringLiteral("/side/added.txt"))); }));
	CHECK(h.panel().watchKeepsViewCurrent());
}

TEST_CASE("CPanel - the watch keeps a folder and a flattened tree current but not search results", "[panel][watcher]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	CHECK(h.panel().watchKeepsViewCurrent());

	h.panel().showAllFilesFromCurrentFolderAndBelow();
	h.settle();
	CHECK(h.panel().watchKeepsViewCurrent()); // A local folder's notifications report every change

	h.panel().showSearchResults({ file });
	h.settle();
	CHECK_FALSE(h.panel().watchKeepsViewCurrent());
}

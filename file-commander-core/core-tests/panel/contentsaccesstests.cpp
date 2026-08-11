#include "paneltesthelpers.h"

TEST_CASE("CPanel - a panel with no committed listing exposes nothing", "[panel][contents]")
{
	PanelHarness h;

	CHECK(h.panel().list().empty());
	CHECK(h.panel().itemHashes().empty());
	CHECK_FALSE(h.panel().itemHashExists(1));
	CHECK_FALSE(h.panel().itemByHash(1).isValid());
	CHECK(h.panel().itemPathByHash(1).isEmpty());
}

TEST_CASE("CPanel - itemPathsByHashes keeps its result aligned with the request", "[panel][contents]")
{
	TempTree tree;
	const QString file = tree.makeFile(QStringLiteral("a.txt"));
	const QString sub = tree.makeDir(QStringLiteral("sub"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	const std::vector<qulonglong> hashes{ hashOf(file), 1 /* in no listing */, hashOf(sub) };

	auto paths = h.panel().itemPathsByHashes(hashes);
	REQUIRE(paths.size() == hashes.size());
	CHECK(paths[0] == file);
	CHECK(paths[1].isEmpty());
	CHECK(paths[2] == sub + '/');

	// Between folders nothing resolves, but the caller still gets one entry per hash it asked about.
	h.worker().close();
	REQUIRE(h.panel().setPath(sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);

	paths = h.panel().itemPathsByHashes(hashes);
	REQUIRE(paths.size() == hashes.size());
	CHECK(std::all_of(paths.begin(), paths.end(), [](const QString& path) { return path.isEmpty(); }));
}

TEST_CASE("CPanel - hidden entries follow the setting", "[panel][contents]")
{
	TempTree tree;
	const QString visible = tree.makeFile(QStringLiteral("visible.txt"));
	const QString hidden = tree.makeFile(QStringLiteral(".hidden.txt"));
	REQUIRE(setFileHidden(hidden));

	// The core must actually regard the file as hidden, or there is nothing for the exclusion below to test.
	if (!CFileSystemObject{ hidden }.isHidden())
	{
		WARN("The filesystem does not report the test file as hidden, skipping");
		return;
	}

	{
		const ShowHiddenFilesSetting setting{ false };
		PanelHarness h;
		REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
		h.panel().setActive(true);
		h.settle();

		CHECK(h.panel().itemHashExists(hashOf(visible)));
		CHECK_FALSE(h.panel().itemHashExists(hashOf(hidden)));
	}

	{
		const ShowHiddenFilesSetting setting{ true };
		PanelHarness h;
		REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
		h.panel().setActive(true);
		h.settle();

		CHECK(h.panel().itemHashExists(hashOf(visible)));
		CHECK(h.panel().itemHashExists(hashOf(hidden)));
	}
}

TEST_CASE("CPanel - the filesystem root has no self-referential parent row", "[panel][contents]")
{
	PanelHarness h;
	REQUIRE(h.panel().setPath(QDir::rootPath(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	// The root's parent is itself, so a [..] row there would be an item that navigates nowhere.
	for (const qulonglong hash : h.panel().itemHashes())
		CHECK_FALSE(h.panel().itemPathByHash(hash).contains(QStringLiteral("..")));
}

TEST_CASE("CPanel - displayDirSize fills in the size of a listed folder", "[panel][contents]")
{
	TempTree tree;
	const QString sub = tree.makeDir(QStringLiteral("sub"));
	tree.makeFile(QStringLiteral("sub/data.bin"), QByteArray(1024, 'x'));
	const QString file = tree.makeFile(QStringLiteral("a.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	// A folder's size is not part of a listing; it is filled in on demand.
	REQUIRE(h.panel().itemByHash(hashOf(sub)).size() == 0);
	h.listener().clear();

	h.panel().displayDirSize(hashOf(sub));
	h.settle();

	CHECK(h.panel().itemByHash(hashOf(sub)).size() >= 1024);
	CHECK(h.listener().count(PanelEvent::ContentsChanged) >= 1);

	// Nothing to calculate for a file, so nothing is reported either.
	h.listener().clear();
	h.panel().displayDirSize(hashOf(file));
	h.settle();
	CHECK(h.listener().count(PanelEvent::ContentsChanged) == 0);
}

#include "paneltesthelpers.h"

#include "filelist/filelistsorter.h"


// As CFileSystemObject::fullAbsolutePath(), which a search reports: a folder's path ends with '/'
[[nodiscard]] static QString searchResultPath(const QString& path)
{
	return CFileSystemObject{ path }.fullAbsolutePath();
}

// As the name column shows them, in display order
[[nodiscard]] static QStringList shownNames(const CPanel& panel)
{
	QStringList names;
	for (const CFileSystemObject& item : panel.visibleItems())
	{
		QString name = fileListFolderPrefix(item, panel.currentDirPathPosix());
		name += item.fullName();
		names.push_back(name);
	}
	return names;
}

[[nodiscard]] static QString native(const QString& path)
{
	return QDir::toNativeSeparators(path);
}

TEST_CASE("CPanel - search results list the paths that exist, in the deepest folder holding them all", "[panel][searchresults]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("a/x"));
	const QString sub = tree.makeDir(QStringLiteral("a/y/sub"));
	const QString one = tree.makeFile(QStringLiteral("a/x/one.txt"));
	const QString two = tree.makeFile(QStringLiteral("a/y/two.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.panel().setSorting(FileListSortField::Name, Qt::AscendingOrder);
	h.settle();

	h.panel().showSearchResults({ searchResultPath(one), searchResultPath(two), searchResultPath(sub), tree.path(QStringLiteral("a/gone.txt")) });
	h.settle();

	CHECK(h.panel().displayMode() == CPanel::SearchResultsMode);
	CHECK(h.panel().currentDirPathPosix() == tree.path(QStringLiteral("a/")));
	CHECK(shownNames(h.panel()) == QStringList{ native(QStringLiteral("y/sub")), native(QStringLiteral("x/one.txt")), native(QStringLiteral("y/two.txt")) });
}

TEST_CASE("CPanel - search results drop a vanished item when listed again, and keep a renamed one", "[panel][searchresults]")
{
	TempTree tree;
	const QString gone = tree.makeFile(QStringLiteral("gone.txt"));
	const QString folder = searchResultPath(tree.makeDir(QStringLiteral("dir")));
	const QString inner = tree.makeFile(QStringLiteral("dir/inner.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.panel().setSorting(FileListSortField::Name, Qt::AscendingOrder);
	h.settle();

	h.panel().showSearchResults({ searchResultPath(gone), folder, searchResultPath(inner) });
	h.settle();
	REQUIRE(shownNames(h.panel()) == QStringList{ QStringLiteral("dir"), native(QStringLiteral("dir/inner.txt")), QStringLiteral("gone.txt") });

	REQUIRE(QFile::remove(gone));
	h.panel().refreshFileList(refreshCauseOther);
	h.settle();
	CHECK(shownNames(h.panel()) == QStringList{ QStringLiteral("dir"), native(QStringLiteral("dir/inner.txt")) });

	const QString renamedFolder = tree.path(QStringLiteral("renamed"));
	REQUIRE(QDir{}.rename(tree.path(QStringLiteral("dir")), renamedFolder));
	h.panel().itemRenamed(folder, searchResultPath(renamedFolder));
	h.settle();
	CHECK(shownNames(h.panel()) == QStringList{ QStringLiteral("renamed"), native(QStringLiteral("renamed/inner.txt")) });
}

TEST_CASE("CPanel - new search results in the same folder replace the old ones at once, with nothing selected", "[panel][searchresults]")
{
	TempTree tree;
	const QString kept = tree.makeFile(QStringLiteral("kept.txt"));
	const QString dropped = tree.makeFile(QStringLiteral("dropped.txt"));
	const QString added = tree.makeFile(QStringLiteral("added.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	h.panel().showSearchResults({ searchResultPath(kept), searchResultPath(dropped) });
	h.settle();
	h.panel().setItemsSelected({ CFileSystemObject{ kept }.hash() }, true);
	REQUIRE(h.panel().selectedItemHashes().size() == 1);

	h.worker().close();
	h.panel().showSearchResults({ searchResultPath(kept), searchResultPath(added) });
	CHECK_FALSE(hasCommittedContents(h.panel()));
	CHECK(h.panel().selectedItemHashes().empty());

	h.worker().open();
	h.settle();
	CHECK(h.panel().currentDirPathPosix() == tree.path() + '/');
	CHECK(shownNames(h.panel()) == QStringList{ QStringLiteral("added.txt"), QStringLiteral("kept.txt") });
	CHECK(h.panel().selectedItemHashes().empty());
}

TEST_CASE("CPanel - going up from search results shows their folder's own listing", "[panel][searchresults]")
{
	TempTree tree;
	tree.makeDir(QStringLiteral("sub"));
	const QString file = tree.makeFile(QStringLiteral("sub/file.txt"));

	PanelHarness h;
	REQUIRE(h.panel().setPath(tree.path(), refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();

	h.panel().showSearchResults({ searchResultPath(file) });
	h.settle();
	REQUIRE(h.panel().currentDirPathPosix() == tree.path(QStringLiteral("sub/")));

	h.panel().navigateUp();
	h.settle();
	CHECK(h.panel().displayMode() == CPanel::NormalMode);
	CHECK(h.panel().currentDirPathPosix() == tree.path(QStringLiteral("sub/")));
	CHECK(hasCommittedContents(h.panel()));
}

#include "paneltesthelpers.h"

#include "filesearchengine/filesearchquery.h"
#include "filesystemhelperfunctions.h"

#include <algorithm>
#include <vector>

[[nodiscard]] static qulonglong cdUpHash(const CPanel& panel)
{
	qulonglong hash = 0;
	panel.readCommittedContents([&hash](const QString&, const FileListHashMap& contents) {
		for (const auto& [itemHash, item] : contents)
		{
			if (item.isCdUp())
				hash = itemHash;
		}
	});
	return hash;
}

[[nodiscard]] static std::vector<QString> pathsOf(const std::vector<CFileSystemObject>& items)
{
	std::vector<QString> paths;
	for (const CFileSystemObject& item : items)
		paths.push_back(item.fullAbsolutePath());
	return paths;
}

[[nodiscard]] static std::vector<QString> namesOf(const std::vector<CFileSystemObject>& items)
{
	std::vector<QString> names;
	for (const CFileSystemObject& item : items)
		names.push_back(item.fullName().toString());
	return names;
}

[[nodiscard]] static std::vector<qulonglong> sorted(std::vector<qulonglong> hashes)
{
	std::ranges::sort(hashes);
	return hashes;
}

// A listed panel showing tree's subfolder "folder": its listing has [..]
struct ListedFolder
{
	TempTree tree;
	const QString folder = tree.makeDir(QStringLiteral("folder"));
	const QString a = tree.makeFile(QStringLiteral("folder/a.txt"), "1");
	const QString b = tree.makeFile(QStringLiteral("folder/b.doc"), "22");
	const QString sub = tree.makeDir(QStringLiteral("folder/sub"));
	PanelHarness h;

	ListedFolder()
	{
		REQUIRE(h.panel().setPath(folder, refreshCauseOther) == FileOperationResultCode::Ok);
		h.panel().setActive(true);
		h.settle();
	}
};

TEST_CASE("CPanel - only listed items other than [..] can be selected", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	const qulonglong cdUp = cdUpHash(panel);
	REQUIRE(cdUp != 0);

	panel.setItemsSelected({ hashOf(f.a), cdUp, 1 /* in no listing */, hashOf(f.sub) }, true);
	CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(f.a), hashOf(f.sub) }));

	panel.setItemsSelected({ hashOf(f.a) }, false);
	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.sub) });
}

TEST_CASE("CPanel - the name filter hides items from the selection and from the command targets", "[panel][selection][filter]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.a), hashOf(f.b) }, true);

	panel.setNameFilter(QStringLiteral("*.txt"));
	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.a) });

	// A hidden item cannot be selected
	panel.setItemsSelected({ hashOf(f.b) }, true);
	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.a) });

	// Showing it again does not select it again
	panel.setNameFilter({});
	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.a) });

	SECTION("A hidden current item is no command target")
	{
		panel.setItemsSelected({ hashOf(f.a) }, false);
		panel.setCurrentItemHashForFolder(f.folder, hashOf(f.b), false);
		CHECK(pathsOf(panel.selectedItemsOrCurrentItem()) == std::vector<QString>{ f.b });

		panel.setNameFilter(QStringLiteral("*.txt"));
		CHECK_FALSE(panel.currentItem().isValid());
		CHECK(panel.selectedItemsOrCurrentItem().empty());
	}
}

TEST_CASE("CPanel - the current item stands in for an empty selection, except [..]", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();

	panel.setCurrentItemHashForFolder(f.folder, cdUpHash(panel), false);
	CHECK(panel.selectedItemsOrCurrentItem().empty());
	CHECK(panel.selectedItems().empty());

	panel.setCurrentItemHashForFolder(f.folder, hashOf(f.a), false);
	CHECK(pathsOf(panel.selectedItemsOrCurrentItem()) == std::vector<QString>{ f.a });
	CHECK(panel.selectedItems().empty());

	// A selection replaces the current item
	panel.setItemsSelected({ hashOf(f.b) }, true);
	CHECK(pathsOf(panel.selectedItemsOrCurrentItem()) == std::vector<QString>{ f.b });
}

TEST_CASE("CPanel - selected items come in display order", "[panel][selection][sort]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.b), hashOf(f.sub), hashOf(f.a) }, true);

	// Folders first in either direction
	panel.setSorting(FileListSortField::Name, Qt::AscendingOrder);
	CHECK(pathsOf(panel.selectedItems()) == std::vector<QString>{ f.sub + '/', f.a, f.b });

	panel.setSorting(FileListSortField::Size, Qt::DescendingOrder);
	CHECK(pathsOf(panel.selectedItems()) == std::vector<QString>{ f.sub + '/', f.b, f.a });
}

TEST_CASE("CPanel - the visible items are the ones the name filter shows, in display order", "[panel][filter][sort]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();

	panel.setSorting(FileListSortField::Name, Qt::DescendingOrder);
	CHECK(namesOf(panel.visibleItems()) == std::vector<QString>{ "..", "sub", "b.doc", "a.txt" });

	panel.setNameFilter(QStringLiteral("*.txt;sub"));
	CHECK(namesOf(panel.visibleItems()) == std::vector<QString>{ "sub", "a.txt" });
}

TEST_CASE("CPanel - inverting the selection skips [..] and hidden items", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.a) }, true);
	panel.setNameFilter(QStringLiteral("*.*"));

	panel.invertSelection();
	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.b) });
}

TEST_CASE("CPanel - a pattern selects and deselects what it matches among the selectable items", "[panel][selection][filter]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();

	SECTION("The name filter's language: ';' separates patterns, case is ignored, an unanchored end matches anywhere")
	{
		panel.setItemsSelectedByPattern(QStringLiteral("*.TXT;su"), true, false);
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(f.a), hashOf(f.sub) }));
	}

	SECTION("Never [..] or an item the name filter hides")
	{
		panel.setNameFilter(QStringLiteral("*.txt;sub"));
		panel.setItemsSelectedByPattern(QStringLiteral("*"), true, false);
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(f.a), hashOf(f.sub) }));
	}

	SECTION("Files only leaves folders out, when selecting and when deselecting")
	{
		panel.setItemsSelectedByPattern(QStringLiteral("*"), true, true);
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(f.a), hashOf(f.b) }));

		panel.setItemsSelected({ hashOf(f.sub) }, true);
		panel.setItemsSelectedByPattern(QStringLiteral("*"), false, true);
		CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.sub) });
	}

	SECTION("Deselecting keeps the items the pattern does not match")
	{
		panel.setItemsSelectedByPattern(QStringLiteral("*"), true, false);
		panel.setItemsSelectedByPattern(QStringLiteral("*.doc"), false, false);
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(f.a), hashOf(f.sub) }));
	}
}

TEST_CASE("CPanel - selecting by extension adds the files with the given file's extension", "[panel][selection]")
{
	TempTree tree;
	const QString folder = tree.makeDir(QStringLiteral("folder"));
	const QString lowerTxt = tree.makeFile(QStringLiteral("folder/a.txt"), "1");
	const QString upperTxt = tree.makeFile(QStringLiteral("folder/B.TXT"), "1");
	const QString doc = tree.makeFile(QStringLiteral("folder/c.doc"), "1");
	const QString makefile = tree.makeFile(QStringLiteral("folder/Makefile"), "1");
	const QString readme = tree.makeFile(QStringLiteral("folder/README"), "1");
	// A folder has no extension, whatever its name
	const QString dottedFolder = tree.makeDir(QStringLiteral("folder/d.txt"));
	PanelHarness h;
	REQUIRE(h.panel().setPath(folder, refreshCauseOther) == FileOperationResultCode::Ok);
	h.panel().setActive(true);
	h.settle();
	CPanel& panel = h.panel();

	SECTION("Ignoring case, and never a folder")
	{
		panel.selectFilesWithExtensionOf(hashOf(lowerTxt));
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(lowerTxt), hashOf(upperTxt) }));
	}

	SECTION("A file without an extension selects the files without one")
	{
		panel.selectFilesWithExtensionOf(hashOf(makefile));
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(makefile), hashOf(readme) }));
	}

	SECTION("The selection grows")
	{
		panel.setItemsSelected({ hashOf(doc) }, true);
		panel.selectFilesWithExtensionOf(hashOf(upperTxt));
		CHECK(sorted(panel.selectedItemHashes()) == sorted({ hashOf(doc), hashOf(lowerTxt), hashOf(upperTxt) }));
	}

	SECTION("A folder selects nothing")
	{
		panel.selectFilesWithExtensionOf(hashOf(dottedFolder));
		CHECK(panel.selectedItemHashes().empty());
	}
}

TEST_CASE("CPanel - the path lines hold the selected items, or the current item", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	const auto pathLine = [](const QString& path) { return shellQuotedPath(toNativeSeparators(path)); };

	SECTION("The selection, in display order, one path per line")
	{
		panel.setSorting(FileListSortField::Name, Qt::DescendingOrder);
		panel.setItemsSelected({ hashOf(f.a), hashOf(f.b) }, true);
		CHECK(panel.selectedOrCurrentItemPathLines() == pathLine(f.b) + u'\n' + pathLine(f.a));
	}

	SECTION("With nothing selected, the current item, even [..]")
	{
		panel.setCurrentItemHashForFolder(f.folder, hashOf(f.a), false);
		CHECK(panel.selectedOrCurrentItemPathLines() == pathLine(f.a));

		const qulonglong cdUp = cdUpHash(panel);
		panel.setCurrentItemHashForFolder(f.folder, cdUp, false);
		CHECK(panel.selectedOrCurrentItemPathLines() == pathLine(panel.itemByHash(cdUp).fullAbsolutePath()));
	}

	SECTION("Empty with no current item either")
	{
		panel.setCurrentItemHashForFolder(f.folder, 0, false);
		CHECK(panel.selectedOrCurrentItemPathLines().isEmpty());
	}
}

TEST_CASE("File search - the \"Search in\" field starts with the panel's selected items, or else its folder", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();

	CHECK(searchInFieldTextFor(panel) == panel.currentDirPathNative());

	panel.setSorting(FileListSortField::Name, Qt::DescendingOrder);
	panel.setItemsSelected({ hashOf(f.a), hashOf(f.b) }, true);
	CHECK(searchInFieldTextFor(panel) == toNativeSeparators(f.b) + QStringLiteral("; ") + toNativeSeparators(f.a));
	CHECK(rootsFromSearchInField(searchInFieldTextFor(panel), f.folder) == QStringList{ f.b, f.a });
}

TEST_CASE("CPanel - the selection summary counts files, folders and their size", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.a), hashOf(f.b), hashOf(f.sub) }, true);

	const FolderContentsSummary summary = panel.selectionSummary();
	CHECK(summary.numFiles == 2);
	CHECK(summary.numFolders == 1);
	CHECK(summary.size == 3);
}

TEST_CASE("CPanel - a refresh keeps the selected items it still lists", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.a), hashOf(f.b) }, true);

	REQUIRE(QFile::remove(f.b));
	panel.refreshFileList(refreshCauseOther);
	f.h.settle();

	CHECK(panel.selectedItemHashes() == std::vector<qulonglong>{ hashOf(f.a) });
}

TEST_CASE("CPanel - navigating to another folder clears the selection", "[panel][selection]")
{
	ListedFolder f;
	CPanel& panel = f.h.panel();
	panel.setItemsSelected({ hashOf(f.a) }, true);

	// Nothing is selected while no listing belongs to the view
	f.h.worker().close();
	REQUIRE(panel.setPath(f.sub, refreshCauseForwardNavigation) == FileOperationResultCode::Ok);
	CHECK(panel.selectedItemHashes().empty());
	CHECK(panel.selectedItems().empty());
	f.h.worker().open();
	f.h.settle();

	REQUIRE(panel.setPath(f.folder, refreshCauseCdUp) == FileOperationResultCode::Ok);
	f.h.settle();
	CHECK(panel.selectedItemHashes().empty());
}

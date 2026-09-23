#include "filelisttesthelpers.h"

#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "qt_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QItemSelectionModel>
#include <QLocale>
#include <QMimeData>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <thread>
#include <vector>

TEST_CASE("Rows sort [..] first, then folders, then files, in either direction", "[filelist][sort]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.setRows({ makeRow(File, "z", "txt"), makeRow(Directory, "b"), makeCdUpRow(), makeRow(File, "a", "txt"), makeRow(Directory, "a"), makeRow(File, "m", "doc") });

	model.sort(NameColumn, Qt::AscendingOrder);
	CHECK(displayedNames(model) == QStringList{ "..", "a", "b", "a.txt", "m.doc", "z.txt" });

	model.sort(NameColumn, Qt::DescendingOrder);
	CHECK(displayedNames(model) == QStringList{ "..", "b", "a", "z.txt", "m.doc", "a.txt" });
}

TEST_CASE("Each column sorts by its own key", "[filelist][sort]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;

	SECTION("Name, numbers by value")
	{
		model.setRows({ makeRow(File, "file10"), makeRow(File, "file2"), makeRow(File, "file1") });
		model.sort(NameColumn, Qt::AscendingOrder);
		CHECK(displayedNames(model) == QStringList{ "file1", "file2", "file10" });
	}

	SECTION("Extension, then name; a dotfile has none")
	{
		model.setRows({ makeRow(File, "b", "txt"), makeRow(File, "c", "doc"), makeRow(File, "", "bashrc"), makeRow(File, "a", "doc") });
		model.sort(ExtColumn, Qt::AscendingOrder);
		CHECK(displayedNames(model) == QStringList{ ".bashrc", "a.doc", "c.doc", "b.txt" });
	}

	SECTION("Folders by name in the extension column")
	{
		model.setRows({ makeRow(Directory, "b"), makeRow(Directory, "a"), makeRow(File, "z", "doc") });
		model.sort(ExtColumn, Qt::AscendingOrder);
		CHECK(displayedNames(model) == QStringList{ "a", "b", "z.doc" });
	}

	SECTION("Size; equal sizes by path")
	{
		model.setRows({ makeRow(File, "big", "", 30u), makeRow(File, "b", "", 5u), makeRow(File, "small", "", 10u), makeRow(File, "a", "", 5u) });
		model.sort(SizeColumn, Qt::AscendingOrder);
		CHECK(displayedNames(model) == QStringList{ "a", "b", "small", "big" });

		model.sort(SizeColumn, Qt::DescendingOrder);
		CHECK(displayedNames(model) == QStringList{ "big", "small", "b", "a" });
	}

	SECTION("Modification time")
	{
		model.setRows({ makeRow(File, "new", "", 0u, 300), makeRow(File, "old", "", 0u, 100), makeRow(File, "mid", "", 0u, 200) });
		model.sort(DateColumn, Qt::AscendingOrder);
		CHECK(displayedNames(model) == QStringList{ "old", "mid", "new" });
	}
}

TEST_CASE("Numbers in names sort by value in the C locale too", "[filelist][sort]")
{
	const QLocale previousLocale;
	QLocale::setDefault(QLocale::c());

	// The collators are built once per thread, so only a new thread builds them under the C locale
	QStringList names;
	std::thread{ [&names] {
		CFileListModel model{ nullptr };
		model.setRows({ makeRow(File, "file10"), makeRow(File, "file2"), makeRow(File, "file1") });
		names = displayedNames(model);
	} }.join();

	QLocale::setDefault(previousLocale);
	CHECK(names == QStringList{ "file1", "file2", "file10" });
}

TEST_CASE("A dotfile displays its whole name and no extension", "[filelist]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.setRows({ makeRow(File, "", "bashrc"), makeRow(File, "notes", "txt"), makeRow(Directory, "folder") });
	model.sort(NameColumn, Qt::AscendingOrder);

	const QModelIndex folder = model.indexByHash(pathHash("/folder/folder/"));
	const QModelIndex dotfile = model.indexByHash(pathHash("/folder/.bashrc"));
	const QModelIndex notes = model.indexByHash(pathHash("/folder/notes.txt"));
	REQUIRE(folder.isValid());
	REQUIRE(dotfile.isValid());
	REQUIRE(notes.isValid());

	CHECK(model.data(folder.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == "[folder]");
	CHECK(model.data(dotfile.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == ".bashrc");
	CHECK(!model.data(dotfile.siblingAtColumn(ExtColumn), Qt::DisplayRole).isValid());
	CHECK(model.data(notes.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == "notes");
	CHECK(model.data(notes.siblingAtColumn(ExtColumn), Qt::DisplayRole).toString() == "txt");
	CHECK(model.data(dotfile, Qt::EditRole).toString() == ".bashrc");
}

TEST_CASE("The name filter hides the rows whose full name doesn't match", "[filelist][filter]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.setRows({ makeCdUpRow(), makeRow(Directory, "docs"), makeRow(File, "a", "txt"), makeRow(File, "b", "TXT"), makeRow(File, "c", "doc") });
	model.sort(NameColumn, Qt::AscendingOrder);

	model.setNameFilter("*.txt");
	CHECK(displayedNames(model) == QStringList{ "a.txt", "b.TXT" });
	CHECK(!model.indexByHash(pathHash("/folder/c.doc")).isValid());
	CHECK(model.contentsSummary().numFiles == 3u); // The totals ignore the filter

	model.setNameFilter("doc"); // Unanchored
	CHECK(displayedNames(model) == QStringList{ "docs", "c.doc" });

	model.setNameFilter({});
	CHECK(model.rowCount() == 5);
}

TEST_CASE("Selection and cursor follow their rows through sort and filter changes", "[filelist][sort][filter]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.setRows({ makeRow(File, "a", "txt"), makeRow(File, "b", "txt"), makeRow(File, "c", "doc"), makeRow(File, "d", "doc") });
	model.sort(NameColumn, Qt::AscendingOrder);

	QItemSelectionModel selection{ &model };
	const qulonglong selectedHash = pathHash("/folder/b.txt");
	const qulonglong currentHash = pathHash("/folder/c.doc");
	selection.select(model.indexByHash(selectedHash), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	selection.setCurrentIndex(model.indexByHash(currentHash), QItemSelectionModel::NoUpdate);

	const auto selectedHashes = [&] {
		std::vector<qulonglong> hashes;
		for (const QModelIndex& index : selection.selectedRows())
			hashes.push_back(model.itemHash(index));
		return hashes;
	};

	model.sort(NameColumn, Qt::DescendingOrder);
	CHECK(selectedHashes() == std::vector<qulonglong>{ selectedHash });
	CHECK(model.itemHash(selection.currentIndex()) == currentHash);

	model.sort(ExtColumn, Qt::AscendingOrder);
	CHECK(selectedHashes() == std::vector<qulonglong>{ selectedHash });
	CHECK(model.itemHash(selection.currentIndex()) == currentHash);

	model.setNameFilter("*.txt");
	CHECK(selectedHashes() == std::vector<qulonglong>{ selectedHash });
	CHECK(!selection.currentIndex().isValid()); // Its row is hidden

	model.setNameFilter("a.*");
	CHECK(selectedHashes().empty());
}

TEST_CASE("Rows are found by hash and by kind", "[filelist]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;

	model.setRows({ makeRow(Directory, "b"), makeRow(Directory, "a") });
	CHECK(model.firstFileRow() == -1);

	model.setRows({ makeRow(File, "x", "txt", 7u), makeCdUpRow(), makeRow(Directory, "b", "", 100u), makeRow(Directory, "a"), makeRow(File, "y", "", 3u) });
	model.sort(NameColumn, Qt::DescendingOrder);
	CHECK(model.firstFileRow() == 3);
	CHECK(model.rowAt(model.firstFileRow()).fullName == "y");

	const QModelIndex x = model.indexByHash(pathHash("/folder/x.txt"));
	REQUIRE(x.isValid());
	CHECK(model.rowAt(x).fullName == "x.txt");
	CHECK(model.itemHash(x) == pathHash("/folder/x.txt"));
	CHECK(!model.indexByHash(pathHash("/folder/missing")).isValid());
	CHECK(model.itemHash(QModelIndex{}) == 0u);

	// [..] is not counted; a folder's calculated size is
	const FolderContentsSummary& summary = model.contentsSummary();
	CHECK(summary.numFiles == 2u);
	CHECK(summary.numFolders == 2u);
	CHECK(summary.size == 110u);

	std::unique_ptr<QMimeData> mime{ model.mimeData({ x }) };
	REQUIRE(mime->urls().size() == 1);
	CHECK(mime->urls().front().toLocalFile() == "/folder/x.txt");
}

TEST_CASE("A row copies what it shows from its object", "[filelist]")
{
	CFileSystemObjectProperties properties;
	properties.fullPath = QStringLiteral("/folder/report.final.txt");
	properties.fullName = QStringLiteral("report.final.txt");
	properties.completeBaseName = QStringLiteral("report.final");
	properties.extension = QStringLiteral("txt");
	properties.type = File;
	properties.exists = true;
	properties.size = 42u;
	properties.modificationTime = 1000;

	const FileListRow row = FileListRow::fromObject(CFileSystemObject{ properties });
	CHECK(row.fullPath == properties.fullPath);
	CHECK(row.fullName == properties.fullName);
	CHECK(row.name == properties.completeBaseName);
	CHECK(row.extension == properties.extension);
	CHECK(row.hash == pathHash(properties.fullPath));
	CHECK(row.size == 42u);
	CHECK(row.modificationTime == 1000);
	CHECK(row.type == File);
	CHECK(!row.isCdUp);
}

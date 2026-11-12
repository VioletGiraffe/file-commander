#include "filelisttesthelpers.h"

#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "qt_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDir>
#include <QItemSelectionModel>
#include <QMimeData>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <vector>

// Only Windows splits a leading dot off as an extension
#ifdef _WIN32
TEST_CASE("A file with nothing before its extension displays its full name", "[filelist]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.setRows({ makeRow(File, "", "jpg"), makeRow(File, "notes", "txt"), makeRow(Directory, "folder") });
	model.sort(NameColumn, Qt::AscendingOrder);

	const QModelIndex folder = model.indexByHash(pathHash("/folder/folder/"));
	const QModelIndex unnamed = model.indexByHash(pathHash("/folder/.jpg"));
	const QModelIndex notes = model.indexByHash(pathHash("/folder/notes.txt"));
	REQUIRE(folder.isValid());
	REQUIRE(unnamed.isValid());
	REQUIRE(notes.isValid());

	CHECK(model.data(folder.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == "[folder]");
	CHECK(model.data(unnamed.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == ".jpg");
	CHECK(model.data(unnamed.siblingAtColumn(ExtColumn), Qt::DisplayRole).toString() == "jpg");
	CHECK(model.data(notes.siblingAtColumn(NameColumn), Qt::DisplayRole).toString() == "notes");
	CHECK(model.data(notes.siblingAtColumn(ExtColumn), Qt::DisplayRole).toString() == "txt");
	CHECK(model.data(unnamed, Qt::EditRole).toString() == ".jpg");
}
#endif

TEST_CASE("A row outside the listed folder shows its folder relative to it before its name", "[filelist]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	CFileSystemObjectProperties nestedFile = rowProperties(File, "nested", "txt");
	nestedFile.fullPath = QStringLiteral("/folder/sub/nested.txt");
	CFileSystemObjectProperties nestedFolder = rowProperties(Directory, "inner");
	nestedFolder.fullPath = QStringLiteral("/folder/sub/inner/");
	model.setRows({ CFileSystemObject{ nestedFile }, CFileSystemObject{ nestedFolder }, makeRow(File, "own", "txt") }, QStringLiteral("/folder/"));

	const auto nameShown = [&model](const char* path) { return model.data(model.indexByHash(pathHash(path)), Qt::DisplayRole).toString(); };
	CHECK(nameShown("/folder/sub/nested.txt") == QDir::toNativeSeparators(QStringLiteral("sub/nested")));
	CHECK(nameShown("/folder/sub/inner/") == QDir::toNativeSeparators(QStringLiteral("[sub/inner]")));
	CHECK(nameShown("/folder/own.txt") == QStringLiteral("own"));
	CHECK(model.data(model.indexByHash(pathHash("/folder/sub/nested.txt")), Qt::EditRole).toString() == QStringLiteral("nested.txt"));
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
	CHECK(model.rowAt(model.firstFileRow()).fullName() == "y");

	const QModelIndex x = model.indexByHash(pathHash("/folder/x.txt"));
	REQUIRE(x.isValid());
	CHECK(model.rowAt(x).fullName() == "x.txt");
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

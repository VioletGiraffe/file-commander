#include "filelisttesthelpers.h"

#include "panel/filelistwidget/cfilelistview.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/catch2/catch.hpp>

#include <QItemSelectionModel>
#include <QLineEdit>
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <tuple>
#include <vector>

extern uint32_t g_randomSeed;

using RowData = std::tuple<QString, uint64_t, time_t, FileSystemObjectType>;

static std::vector<RowData> displayedRowData(const CFileListModel& model)
{
	std::vector<RowData> rows;
	for (int row = 0; row < model.rowCount(); ++row)
	{
		const FileListRow& data = model.rowAt(row);
		rows.emplace_back(data.fullPath, data.size, data.modificationTime, data.type);
	}
	return rows;
}

static std::set<qulonglong> selectedHashes(const CFileListModel& model, const QItemSelectionModel& selection)
{
	std::set<qulonglong> hashes;
	for (const QModelIndex& index : selection.selectedRows())
		hashes.insert(model.itemHash(index));
	return hashes;
}

// By path, so that which rows a step picks depends on the seed alone
using Listing = std::map<QString, FileListRow>;

static std::vector<FileListRow> rowsOf(const Listing& listing)
{
	std::vector<FileListRow> rows;
	rows.reserve(listing.size());
	for (const auto& [path, row] : listing)
		rows.push_back(row);
	return rows;
}

TEST_CASE("An update changes single rows, and selection and cursor stay on theirs", "[filelist][update]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.sort(SizeColumn, Qt::AscendingOrder);
	Listing listing;
	for (const FileListRow& row : { makeCdUpRow(), makeRow(Directory, "d"), makeRow(File, "a", "txt", 1), makeRow(File, "b", "txt", 2), makeRow(File, "c", "txt", 3), makeRow(File, "e", "txt", 4) })
		listing[row.fullPath] = row;
	model.setRows(rowsOf(listing));

	QItemSelectionModel selection{ &model };
	const qulonglong a = pathHash("/folder/a.txt"), b = pathHash("/folder/b.txt"), c = pathHash("/folder/c.txt");
	selection.select(model.indexByHash(a), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	selection.select(model.indexByHash(c), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	selection.setCurrentIndex(model.indexByHash(a), QItemSelectionModel::NoUpdate);

	listing["/folder/a.txt"].size = 10; // Moves below every other file
	listing.erase("/folder/c.txt");
	listing.erase("/folder/e.txt");
	const FileListRow added = makeRow(File, "f", "txt", 0);
	listing[added.fullPath] = added;

	REQUIRE(model.updateRows(rowsOf(listing)));
	CHECK(displayedNames(model) == QStringList{ "..", "d", "f.txt", "b.txt", "a.txt" });
	CHECK(selectedHashes(model, selection) == std::set<qulonglong>{ a });
	CHECK(model.itemHash(selection.currentIndex()) == a);
	CHECK(model.indexByHash(b).row() == 3);
	CHECK(model.contentsSummary().numFiles == 3);
	CHECK(model.contentsSummary().size == 12);
}

TEST_CASE("An update that would change most rows resets instead", "[filelist][update]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	std::vector<FileListRow> rows;
	for (int i = 0; i < 1500; ++i)
		rows.push_back(makeRow(File, QStringLiteral("old%1").arg(i), "txt"));
	model.setRows(rows);

	rows.clear();
	for (int i = 0; i < 1500; ++i)
		rows.push_back(makeRow(File, QStringLiteral("new%1").arg(i), "txt"));

	CHECK_FALSE(model.updateRows(rows));
	CHECK(model.rowCount() == 1500);
	CHECK(model.indexByHash(pathHash("/folder/new0.txt")).isValid());
}

TEST_CASE("Random updates keep the rows, their order and the persistent indexes right", "[filelist][update]")
{
	const int sortColumn = GENERATE(NameColumn, ExtColumn, SizeColumn, DateColumn);
	const Qt::SortOrder sortOrder = GENERATE(Qt::AscendingOrder, Qt::DescendingOrder);
	const QString nameFilter = GENERATE(QString{}, QStringLiteral("*[A-G]*"));

	INFO("Random seed: " << g_randomSeed);
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	Listing listing;
	const auto addRandomRow = [&] {
		const bool folder = random.randomNumber(0, 3) == 0;
		// Few names, sizes and times, so rows often tie on the sort key
		const FileListRow row = makeRow(folder ? Directory : File, random.randomString(2), folder ? QString{} : random.randomString(1),
			random.randomNumber<uint64_t>(0u, 3u), random.randomNumber<time_t>(0, 3));
		listing[row.fullPath] = row;
	};
	const auto randomListedRow = [&] {
		return std::next(listing.begin(), random.randomNumber<ptrdiff_t>(0, (ptrdiff_t)listing.size() - 1));
	};

	listing[makeCdUpRow().fullPath] = makeCdUpRow();
	for (int i = 0; i < 100; ++i)
		addRandomRow();

	TestedModel tested;
	CFileListModel& model = tested.model;
	model.sort(sortColumn, sortOrder);
	model.setNameFilter(nameFilter);
	model.setRows(rowsOf(listing));
	QItemSelectionModel selection{ &model };

	for (int step = 0; step < 50; ++step)
	{
		INFO("Step " << step);
		for (int i = 0; i < 5 && model.rowCount() > 0; ++i)
			selection.select(model.index(random.randomNumber(0, model.rowCount() - 1), 0), QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
		if (model.rowCount() > 0)
			selection.setCurrentIndex(model.index(random.randomNumber(0, model.rowCount() - 1), 0), QItemSelectionModel::NoUpdate);

		const std::set<qulonglong> selectedBefore = selectedHashes(model, selection);
		const qulonglong currentBefore = model.itemHash(selection.currentIndex());

		for (int i = random.randomNumber(0, 10); i > 0 && !listing.empty(); --i)
			listing.erase(randomListedRow());
		for (int i = random.randomNumber(0, 10); i > 0; --i)
			addRandomRow();
		for (int i = random.randomNumber(0, 10); i > 0 && !listing.empty(); --i)
		{
			FileListRow& row = randomListedRow()->second;
			row.size = random.randomNumber<uint64_t>(0u, 3u);
			row.modificationTime = random.randomNumber<time_t>(0, 3);
			// A bundle sorts with the files
			if (row.type == Directory && !row.isCdUp && random.randomNumber(0, 3) == 0)
				row.type = Bundle;
		}

		REQUIRE(model.updateRows(rowsOf(listing)));

		CFileListModel reference{ nullptr };
		reference.sort(sortColumn, sortOrder);
		reference.setNameFilter(nameFilter);
		reference.setRows(rowsOf(listing));
		REQUIRE(displayedRowData(model) == displayedRowData(reference));
		CHECK(model.contentsSummary().numFiles == reference.contentsSummary().numFiles);
		CHECK(model.contentsSummary().numFolders == reference.contentsSummary().numFolders);
		CHECK(model.contentsSummary().size == reference.contentsSummary().size);

		std::set<qulonglong> selectedSurvivors;
		std::copy_if(selectedBefore.cbegin(), selectedBefore.cend(), std::inserter(selectedSurvivors, selectedSurvivors.end()), [&](qulonglong hash) {
			return reference.indexByHash(hash).isValid();
		});
		CHECK(selectedHashes(model, selection) == selectedSurvivors);

		if (reference.indexByHash(currentBefore).isValid())
			CHECK(model.itemHash(selection.currentIndex()) == currentBefore);
	}

	model.setNameFilter({});
	CFileListModel reference{ nullptr };
	reference.sort(sortColumn, sortOrder);
	reference.setRows(rowsOf(listing));
	CHECK(displayedRowData(model) == displayedRowData(reference));
}

TEST_CASE("An open editor keeps its row and its text through an update", "[filelist][update][view]")
{
	CFileListModel model{ nullptr };
	model.sort(SizeColumn, Qt::AscendingOrder);
	Listing listing;
	for (const FileListRow& row : { makeRow(File, "a", "txt", 1), makeRow(File, "b", "txt", 2), makeRow(File, "c", "txt", 3) })
		listing[row.fullPath] = row;
	model.setRows(rowsOf(listing));

	CFileListView view;
	view.setModel(&model);
	view.show();

	const qulonglong edited = pathHash("/folder/b.txt");
	static_cast<QAbstractItemView&>(view).edit(model.indexByHash(edited));
	auto* editor = qobject_cast<QLineEdit*>(view.indexWidget(model.indexByHash(edited)));
	REQUIRE(editor);
	CHECK(editor->text() == "b.txt");
	editor->setText(QStringLiteral("typed"));

	listing["/folder/b.txt"].size = 10; // Moves below c.txt
	const FileListRow added = makeRow(File, "d", "txt", 0);
	listing[added.fullPath] = added;
	REQUIRE(model.updateRows(rowsOf(listing)));

	CHECK(model.indexByHash(edited).row() == 3);
	CHECK(view.indexWidget(model.indexByHash(edited)) == editor);
	CHECK(editor->text() == "typed");
}

TEST_CASE("The rows on screen stay in place through an update", "[filelist][update][view]")
{
	CFileListModel model{ nullptr };
	model.sort(NameColumn, Qt::AscendingOrder);
	Listing listing;
	for (int i = 0; i < 300; ++i)
	{
		const FileListRow row = makeRow(File, QStringLiteral("f%1").arg(i, 3, 10, QChar('0')), "txt");
		listing[row.fullPath] = row;
	}
	model.setRows(rowsOf(listing));

	CFileListView view;
	view.setModel(&model);
	view.resize(400, 300);
	view.show();
	view.scrollTo(model.indexByHash(pathHash("/folder/f150.txt")), QAbstractItemView::PositionAtTop);

	const auto yOf = [&](const char* path) { return view.visualRect(model.indexByHash(pathHash(path))).top(); };
	const int y150 = yOf("/folder/f150.txt"), y151 = yOf("/folder/f151.txt");
	REQUIRE(view.indexAt({ 0, 0 }) == model.indexByHash(pathHash("/folder/f150.txt")));

	SECTION("Rows inserted and removed above the screen")
	{
		for (int i = 0; i < 20; ++i)
		{
			const FileListRow row = makeRow(File, QStringLiteral("e%1").arg(i), "txt"); // Sorts above every f
			listing[row.fullPath] = row;
		}
		for (int i = 0; i < 30; ++i)
			listing.erase(QStringLiteral("/folder/f%1.txt").arg(i, 3, 10, QChar('0')));

		const CFileListView::ScrollPosition position = view.scrollPosition();
		REQUIRE(model.updateRows(rowsOf(listing)));
		REQUIRE(view.restoreScrollPosition(position));

		CHECK(yOf("/folder/f150.txt") == y150);
	}

	SECTION("The top row removed: the rows below it stay")
	{
		listing.erase("/folder/f150.txt");

		const CFileListView::ScrollPosition position = view.scrollPosition();
		REQUIRE(model.updateRows(rowsOf(listing)));
		REQUIRE(view.restoreScrollPosition(position));

		CHECK(yOf("/folder/f151.txt") == y151);
	}

	SECTION("A reset leaves no row to restore")
	{
		const CFileListView::ScrollPosition position = view.scrollPosition();
		model.setRows(rowsOf(listing));
		CHECK_FALSE(view.restoreScrollPosition(position));
	}

	SECTION("The position survives the view showing another model in between, as on a tab switch")
	{
		const CFileListView::ScrollPosition position = view.scrollPosition();
		CFileListModel otherModel{ nullptr };
		otherModel.setRows(rowsOf(listing));
		view.setModel(&otherModel);
		view.scrollToTop();
		view.setModel(&model);

		REQUIRE(view.restoreScrollPosition(position));
		CHECK(yOf("/folder/f150.txt") == y150);
	}
}

// Only the update is timed; building the listings and the model beforehand is not
TEST_CASE("Timings of an update against a reset", "[.][filelist][update][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	std::printf("%8s %8s %12s %12s\n", "rows", "changes", "update, ms", "reset, ms");
	for (const int numRows : { 1'000, 10'000, 100'000 })
	{
		std::vector<FileListRow> baseRows;
		baseRows.reserve((size_t)numRows);
		for (int i = 0; i < numRows; ++i)
			baseRows.push_back(makeRow(File, QStringLiteral("file%1").arg(i), random.randomString(3), random.randomNumber<uint64_t>(0u, 1'000'000u)));

		for (const int numChanges : { 30, numRows / 100, numRows / 10, numRows / 4, numRows / 2, numRows })
		{

			// A third each removed, added and resized, at random positions
			std::set<size_t> removedRows;
			while (removedRows.size() < (size_t)numChanges / 3)
				removedRows.insert(random.randomNumber<size_t>(0, baseRows.size() - 1));

			std::vector<FileListRow> changedRows;
			for (size_t i = 0; i < baseRows.size(); ++i)
			{
				if (!removedRows.contains(i))
					changedRows.push_back(baseRows[i]);
			}

			for (int i = 0; i < numChanges / 3; ++i)
				changedRows.push_back(makeRow(File, QStringLiteral("new%1").arg(i), random.randomString(3)));
			for (int i = 0; i < numChanges / 3; ++i)
				changedRows[random.randomNumber<size_t>(0, changedRows.size() - 1)].size = random.randomNumber<uint64_t>(0u, 1'000'000u);

			const auto timeMs = [&](const auto& apply) {
				CFileListModel model{ nullptr };
				model.sort(ExtColumn, Qt::AscendingOrder);
				model.setRows(baseRows);
				QTreeView view;
				view.setUniformRowHeights(true);
				view.setModel(&model);
				(void)view.indexAt({ 0, 0 }); // Lays the rows out

				std::vector<FileListRow> rows = changedRows;
				const auto start = std::chrono::steady_clock::now();
				apply(model, std::move(rows));
				(void)view.indexAt({ 0, 0 });
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			};

			const double updateMs = timeMs([](CFileListModel& model, std::vector<FileListRow> rows) { (void)model.updateRows(std::move(rows)); });
			const double resetMs = timeMs([](CFileListModel& model, std::vector<FileListRow> rows) { model.setRows(std::move(rows)); });
			std::printf("%8d %8d %12.1f %12.1f\n", numRows, numChanges, updateMs, resetMs);
		}
	}
}

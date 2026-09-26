#include "filelisttesthelpers.h"

#include "panel/filelistwidget/cfilelistview.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QItemSelectionModel>
#include <QLineEdit>
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#include <psapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <iterator>
#include <map>
#include <numeric>
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
		const CFileSystemObject& data = model.rowAt(row);
		rows.emplace_back(data.fullAbsolutePath(), data.size(), data.modificationTime(), data.type());
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
using Listing = std::map<QString, CFileSystemObjectProperties>;

static std::vector<CFileSystemObject> rowsOf(const Listing& listing)
{
	std::vector<CFileSystemObject> rows;
	rows.reserve(listing.size());
	for (const auto& [path, properties] : listing)
		rows.emplace_back(properties);
	return rows;
}

TEST_CASE("An update changes single rows, and selection and cursor stay on theirs", "[filelist][update]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;
	model.sort(SizeColumn, Qt::AscendingOrder);
	Listing listing;
	for (const CFileSystemObjectProperties& row : { cdUpRowProperties(), rowProperties(Directory, "d"), rowProperties(File, "a", "txt", 1), rowProperties(File, "b", "txt", 2),
		rowProperties(File, "c", "txt", 3), rowProperties(File, "e", "txt", 4) })
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
	const CFileSystemObjectProperties added = rowProperties(File, "f", "txt", 0);
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
	std::vector<CFileSystemObject> rows;
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
		const CFileSystemObjectProperties row = rowProperties(folder ? Directory : File, random.randomString(2), folder ? QString{} : random.randomString(1),
			random.randomNumber<uint64_t>(0u, 3u), random.randomNumber<time_t>(0, 3));
		listing[row.fullPath] = row;
	};
	const auto randomListedRow = [&] {
		return std::next(listing.begin(), random.randomNumber<ptrdiff_t>(0, (ptrdiff_t)listing.size() - 1));
	};

	listing[cdUpRowProperties().fullPath] = cdUpRowProperties();
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
			CFileSystemObjectProperties& row = randomListedRow()->second;
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
	for (const CFileSystemObjectProperties& row : { rowProperties(File, "a", "txt", 1), rowProperties(File, "b", "txt", 2), rowProperties(File, "c", "txt", 3) })
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
	const CFileSystemObjectProperties added = rowProperties(File, "d", "txt", 0);
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
		const CFileSystemObjectProperties row = rowProperties(File, QStringLiteral("f%1").arg(i, 3, 10, QChar('0')), "txt");
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
			const CFileSystemObjectProperties row = rowProperties(File, QStringLiteral("e%1").arg(i), "txt"); // Sorts above every f
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
		std::vector<CFileSystemObjectProperties> baseListing;
		baseListing.reserve((size_t)numRows);
		for (int i = 0; i < numRows; ++i)
			baseListing.push_back(rowProperties(File, QStringLiteral("file%1").arg(i), random.randomString(3), random.randomNumber<uint64_t>(0u, 1'000'000u)));
		const std::vector<CFileSystemObject> baseRows(baseListing.cbegin(), baseListing.cend());

		for (const int numChanges : { 30, numRows / 100, numRows / 10, numRows / 4, numRows / 2, numRows })
		{

			// A third each removed, added and resized, at random positions
			std::set<size_t> removedRows;
			while (removedRows.size() < (size_t)numChanges / 3)
				removedRows.insert(random.randomNumber<size_t>(0, baseRows.size() - 1));

			std::vector<CFileSystemObjectProperties> changedListing;
			for (size_t i = 0; i < baseListing.size(); ++i)
			{
				if (!removedRows.contains(i))
					changedListing.push_back(baseListing[i]);
			}

			for (int i = 0; i < numChanges / 3; ++i)
				changedListing.push_back(rowProperties(File, QStringLiteral("new%1").arg(i), random.randomString(3)));
			for (int i = 0; i < numChanges / 3; ++i)
				changedListing[random.randomNumber<size_t>(0, changedListing.size() - 1)].size = random.randomNumber<uint64_t>(0u, 1'000'000u);
			const std::vector<CFileSystemObject> changedRows(changedListing.cbegin(), changedListing.cend());

			const auto timeMs = [&](const auto& apply) {
				CFileListModel model{ nullptr };
				model.sort(ExtColumn, Qt::AscendingOrder);
				model.setRows(baseRows);
				QTreeView view;
				view.setUniformRowHeights(true);
				view.setModel(&model);
				(void)view.indexAt({ 0, 0 }); // Lays the rows out

				std::vector<CFileSystemObject> rows = changedRows;
				const auto start = std::chrono::steady_clock::now();
				apply(model, std::move(rows));
				(void)view.indexAt({ 0, 0 });
				return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			};

			const double updateMs = timeMs([](CFileListModel& model, std::vector<CFileSystemObject> rows) { (void)model.updateRows(std::move(rows)); });
			const double resetMs = timeMs([](CFileListModel& model, std::vector<CFileSystemObject> rows) { model.setRows(std::move(rows)); });
			std::printf("%8d %8d %12.1f %12.1f\n", numRows, numChanges, updateMs, resetMs);
		}
	}
}

// One filter change per keystroke, typing and then erasing. Size sorts by integer, Name and Ext by the collator.
TEST_CASE("Timings of a name filter change", "[.][filelist][filter][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	static constexpr const char* columnNames[NumberOfColumns] = { "name", "ext", "size", "date" };
	const std::vector<QString> keystrokes{ "a", "ab", "abc", "ab", "a", "" };
	const auto msSince = [](std::chrono::steady_clock::time_point start) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	};

	std::printf("%8s %6s %8s %8s %12s %12s\n", "rows", "sort", "filter", "shown", "model, ms", "+ view, ms");
	for (const int numRows : { 1'000, 10'000, 100'000 })
	{
		std::vector<CFileSystemObject> rows;
		rows.reserve((size_t)numRows);
		for (int i = 0; i < numRows; ++i)
			rows.emplace_back(rowProperties(File, random.randomString(12), random.randomString(3), random.randomNumber<uint64_t>(0u, 1'000'000u)));

		for (const int column : { SizeColumn, NameColumn, ExtColumn })
		{
			CFileListModel model{ nullptr };
			model.sort(column, Qt::AscendingOrder);
			model.setRows(rows);

			CFileListModel viewedModel{ nullptr };
			viewedModel.sort(column, Qt::AscendingOrder);
			viewedModel.setRows(rows);
			QTreeView view;
			view.resize(800, 600);
			view.setUniformRowHeights(true);
			view.setModel(&viewedModel);
			view.setCurrentIndex(viewedModel.index(0, 0)); // A cursor, as in a panel: a persistent index to remap
			(void)view.indexAt({ 0, 0 }); // Lays the rows out

			for (const QString& filter : keystrokes)
			{
				auto start = std::chrono::steady_clock::now();
				model.setNameFilter(filter);
				const double modelMs = msSince(start);

				start = std::chrono::steady_clock::now();
				viewedModel.setNameFilter(filter);
				(void)view.indexAt({ 0, 0 });
				const double viewMs = msSince(start);

				REQUIRE(viewedModel.rowCount() == model.rowCount());
				std::printf("%8d %6s %8s %8d %12.1f %12.1f\n", numRows, columnNames[column], filter.isEmpty() ? "(none)" : qUtf8Printable(filter), model.rowCount(), modelMs, viewMs);
			}
		}
	}
}

// Zero where not measured
[[nodiscard]] static size_t processPrivateBytes()
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS_EX counters{};
	if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
		return counters.PrivateUsage;
#endif
	return 0;
}

// Mixed case and digits exercise the collator's case and numeric rules
TEST_CASE("Collation keys order as NaturalSort::compare does", "[filelist][collator]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	std::vector<QString> names;
	for (int i = 0; i < 10'000; ++i)
	{
		const QString name = random.randomString(2) + QString::number(random.randomNumber<int>(0, 2000)) + random.randomString(2);
		names.push_back(i % 2 == 0 ? name : name.toLower());
	}
	names.emplace_back();

	std::vector<QCollatorSortKey> keys;
	for (const QString& name : names)
		keys.push_back(NaturalSort::sortKey(name));

	// Stable, so names that compare equal keep one order in both
	std::vector<uint32_t> byCompare(names.size());
	std::iota(byCompare.begin(), byCompare.end(), 0u);
	std::vector<uint32_t> byKey = byCompare;
	std::stable_sort(byCompare.begin(), byCompare.end(), [&names](uint32_t l, uint32_t r) { return NaturalSort::compare(names[l], names[r]) < 0; });
	std::stable_sort(byKey.begin(), byKey.end(), [&keys](uint32_t l, uint32_t r) { return keys[l].compare(keys[r]) < 0; });
	CHECK(byKey == byCompare);
}

// The model's name sort without the model: indices into names, the ones containing 'A' and then all of them, repeatedly
TEST_CASE("Timings of a collator sort", "[.][filelist][collator][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	// 12 characters as a typical name, 3 as an extension, 255 as the longest name a file system allows
	{
		std::vector<QString> stems, extensions, names255;
		for (int i = 0; i < 100'000; ++i)
		{
			stems.push_back(random.randomString(12));
			extensions.push_back(random.randomString(3));
			names255.push_back(random.randomString(255));
		}

		// Every set of keys stays alive: a freed set's memory would be reused by the next one and hide its growth
		std::vector<std::vector<QCollatorSortKey>> keySets;
		keySets.reserve(3);
		for (const auto* strings : { &stems, &extensions, &names255 })
		{
			const size_t before = processPrivateBytes();
			std::vector<QCollatorSortKey>& keys = keySets.emplace_back();
			keys.reserve(strings->size());
			const auto start = std::chrono::steady_clock::now();
			for (const QString& s : *strings)
				keys.push_back(NaturalSort::sortKey(s));
			const double buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			const double bytesPerKey = (double)(processPrivateBytes() - before) / (double)keys.size();
			std::printf("%zu keys of %lld-character strings: %.1f ms to build, %.0f bytes each\n", keys.size(), (long long)strings->front().size(), buildMs, bytesPerKey);
		}

		// The model's memory besides the rows it is given: sort keys, shared by extension, and the display order
		std::vector<CFileSystemObject> rows;
		rows.reserve(stems.size());
		for (size_t i = 0; i < stems.size(); ++i)
			rows.emplace_back(rowProperties(File, stems[i], extensions[i % 20]));

		CFileListModel model{ nullptr };
		model.sort(ExtColumn, Qt::AscendingOrder);
		const size_t before = processPrivateBytes();
		const auto start = std::chrono::steady_clock::now();
		model.setRows(std::move(rows));
		const double setRowsMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		const double bytesPerRow = (double)(processPrivateBytes() - before) / (double)model.rowCount();
		std::printf("A model of %d rows with 20 extensions, sorted by extension: %.1f ms to set, %.0f bytes per row besides the rows\n", model.rowCount(), setRowsMs, bytesPerRow);
	}

	std::printf("%8s %6s %8s %10s %10s\n", "names", "round", "sorted", "sort, ms", "by key, ms");
	for (const int numNames : { 10'000, 100'000 })
	{
		std::vector<QString> names;
		names.reserve((size_t)numNames);
		for (int i = 0; i < numNames; ++i)
			names.push_back(random.randomString(12) + '.' + random.randomString(3));

		std::vector<uint32_t> all(names.size()), withA;
		for (uint32_t i = 0; i < (uint32_t)names.size(); ++i)
		{
			all[i] = i;
			if (names[i].contains('A'))
				withA.push_back(i);
		}

		std::vector<QCollatorSortKey> keys;
		keys.reserve(names.size());
		for (const QString& name : names)
			keys.push_back(NaturalSort::sortKey(name));

		for (int round = 1; round <= 4; ++round)
		{
			for (const std::vector<uint32_t>* indices : { &withA, &all })
			{
				std::vector<uint32_t> sorted = *indices;
				auto start = std::chrono::steady_clock::now();
				std::sort(sorted.begin(), sorted.end(), [&names](uint32_t l, uint32_t r) { return NaturalSort::compare(names[l], names[r]) < 0; });
				const double sortMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

				std::vector<uint32_t> sortedByKey = *indices;
				start = std::chrono::steady_clock::now();
				std::sort(sortedByKey.begin(), sortedByKey.end(), [&keys](uint32_t l, uint32_t r) { return keys[l].compare(keys[r]) < 0; });
				const double sortByKeyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

				std::printf("%8d %6d %8zu %10.1f %10.1f\n", numNames, round, sorted.size(), sortMs, sortByKeyMs);
			}
		}
	}
}

#include "filelisttesthelpers.h"

#include "panel/filelistwidget/cfilelistview.h"
#include "panel/filelistwidget/filelistlook.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"

#include "detail/file_list_hashmap.h"
#include "filelist/sortedfilelist.h"
#include "panellisting.h"

#include "crandomdatagenerator.h"
#include "qt_helpers.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QColor>
#include <QImage>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPixmap>
#include <QPoint>
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
#include <random>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <tuple>
#include <vector>

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
	for (const CFileSystemObjectProperties& row : { cdUpItemProperties(), itemProperties(Directory, "d"), itemProperties(File, "a", "txt", 1), itemProperties(File, "b", "txt", 2),
		itemProperties(File, "c", "txt", 3), itemProperties(File, "e", "txt", 4) })
		listing[row.fullPath] = row;
	model.setRows(rowsOf(listing), itemsFolder);

	QItemSelectionModel selection{ &model };
	const qulonglong a = pathHash("/folder/a.txt"), b = pathHash("/folder/b.txt"), c = pathHash("/folder/c.txt");
	selection.select(model.indexByHash(a), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	selection.select(model.indexByHash(c), QItemSelectionModel::Select | QItemSelectionModel::Rows);
	selection.setCurrentIndex(model.indexByHash(a), QItemSelectionModel::NoUpdate);

	listing["/folder/a.txt"].size = 10; // Moves below every other file
	listing.erase("/folder/c.txt");
	listing.erase("/folder/e.txt");
	const CFileSystemObjectProperties added = itemProperties(File, "f", "txt", 0);
	listing[added.fullPath] = added;

	REQUIRE(model.updateRows(rowsOf(listing)));
	CHECK(displayedNames(model) == QStringList{ "..", "d", "f.txt", "b.txt", "a.txt" });
	CHECK(selectedHashes(model, selection) == std::set<qulonglong>{ a });
	CHECK(model.itemHash(selection.currentIndex()) == a);
	CHECK(model.indexByHash(b).row() == 3);
	CHECK(model.contentsSummary().numFiles == 3);
	CHECK(model.contentsSummary().size == 12);
}

// Replacing every row changes each twice: one removal, one addition
TEST_CASE("An update past a number of changes resets instead, whatever the share of rows", "[filelist][update]")
{
	TestedModel tested;
	CFileListModel& model = tested.model;

	const auto replaceAllRows = [&model](int numRows) {
		std::vector<CFileSystemObject> rows;
		for (int i = 0; i < numRows; ++i)
			rows.push_back(makeItem(File, QStringLiteral("old%1").arg(i), "txt"));
		model.setRows(rows, itemsFolder);

		rows.clear();
		for (int i = 0; i < numRows; ++i)
			rows.push_back(makeItem(File, QStringLiteral("new%1").arg(i), "txt"));
		return model.updateRows(rows);
	};

	SECTION("2000 changes: an update")
	{
		CHECK(replaceAllRows(1000));
	}

	SECTION("6000 changes: a reset")
	{
		CHECK_FALSE(replaceAllRows(3000));
	}

	CHECK(model.indexByHash(pathHash("/folder/new0.txt")).isValid());
	CHECK_FALSE(model.indexByHash(pathHash("/folder/old0.txt")).isValid());
}

TEST_CASE("Random updates and changes applied in place keep the rows, their order and the persistent indexes right", "[filelist][update]")
{
	const int sortColumn = GENERATE(NameColumn, ExtColumn, SizeColumn, DateColumn);
	const Qt::SortOrder sortOrder = GENERATE(Qt::AscendingOrder, Qt::DescendingOrder);
	const QString nameFilter = GENERATE(QString{}, QStringLiteral("*[A-G]*"));
	const bool inPlace = GENERATE(false, true); // applyChanges() rather than updateRows()

	INFO("Random seed: " << g_randomSeed);
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	Listing listing;
	std::set<QString> touchedPaths; // Since the last update
	const auto addRandomRow = [&] {
		const bool folder = random.randomNumber(0, 3) == 0;
		// Few names, sizes and times, so rows often tie on the sort key
		const QString name = random.randomString(2), extension = folder ? QString{} : random.randomString(1);
		const uint64_t size = random.randomNumber<uint64_t>(0u, 3u);
		const time_t modificationTime = random.randomNumber<time_t>(0, 3);
		const CFileSystemObjectProperties row = itemProperties(folder ? Directory : File, name, extension, size, modificationTime);
		listing[row.fullPath] = row;
		touchedPaths.insert(row.fullPath);
	};
	const auto randomListedRow = [&] {
		return std::next(listing.begin(), random.randomNumber<ptrdiff_t>(0, (ptrdiff_t)listing.size() - 1));
	};

	listing[cdUpItemProperties().fullPath] = cdUpItemProperties();
	for (int i = 0; i < 100; ++i)
		addRandomRow();

	TestedModel tested;
	CFileListModel& model = tested.model;
	model.sort(sortColumn, sortOrder);
	model.setNameFilter(nameFilter);
	model.setRows(rowsOf(listing), itemsFolder);
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
		{
			const auto row = randomListedRow();
			touchedPaths.insert(row->first);
			listing.erase(row);
		}
		for (int i = random.randomNumber(0, 10); i > 0; --i)
			addRandomRow();
		for (int i = random.randomNumber(0, 10); i > 0 && !listing.empty(); --i)
		{
			CFileSystemObjectProperties& row = randomListedRow()->second;
			touchedPaths.insert(row.fullPath);
			row.size = random.randomNumber<uint64_t>(0u, 3u);
			row.modificationTime = random.randomNumber<time_t>(0, 3);
			// A bundle sorts with the files
			if (row.type == Directory && !row.isCdUp && random.randomNumber(0, 3) == 0)
				row.type = Bundle;
		}

		if (inPlace)
		{
			// Each touched path once, as it is now, as the panel reports changes. Repeats change nothing: a row that may be unchanged, a hash no row has.
			if (!listing.empty())
				touchedPaths.insert(randomListedRow()->first);
			std::vector<CFileSystemObject> upserted;
			std::vector<qulonglong> removedHashes{ pathHash("/folder/absent") };
			for (const QString& path : touchedPaths)
			{
				if (const auto row = listing.find(path); row != listing.end())
					upserted.emplace_back(row->second);
				else
					removedHashes.push_back(pathHash(path));
			}

			model.applyChanges(std::move(upserted), removedHashes);
		}
		else
			REQUIRE(model.updateRows(rowsOf(listing)));
		touchedPaths.clear();

		CFileListModel reference{ nullptr };
		reference.sort(sortColumn, sortOrder);
		reference.setNameFilter(nameFilter);
		reference.setRows(rowsOf(listing), itemsFolder);
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

		// rowsOf() indexes every row for this many hashes; indexByHash() searches
		std::vector<qulonglong> listedHashes;
		for (const auto& [path, properties] : listing)
			listedHashes.push_back(pathHash(path));
		const std::vector<int> rows = model.rowsOf(listedHashes);
		for (size_t i = 0; i < listedHashes.size(); ++i)
		{
			CHECK(rows[i] == reference.indexByHash(listedHashes[i]).row());
			CHECK(model.indexByHash(listedHashes[i]).row() == rows[i]);
		}
	}

	model.setNameFilter({});
	CFileListModel reference{ nullptr };
	reference.sort(sortColumn, sortOrder);
	reference.setRows(rowsOf(listing), itemsFolder);
	CHECK(displayedRowData(model) == displayedRowData(reference));
}

TEST_CASE("An open editor keeps its row and its text through an update", "[filelist][update][view]")
{
	CFileListModel model{ nullptr };
	model.sort(SizeColumn, Qt::AscendingOrder);
	Listing listing;
	for (const CFileSystemObjectProperties& row : { itemProperties(File, "a", "txt", 1), itemProperties(File, "b", "txt", 2), itemProperties(File, "c", "txt", 3) })
		listing[row.fullPath] = row;
	model.setRows(rowsOf(listing), itemsFolder);

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
	const CFileSystemObjectProperties added = itemProperties(File, "d", "txt", 0);
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
		const CFileSystemObjectProperties row = itemProperties(File, QStringLiteral("f%1").arg(i, 3, 10, QChar('0')), "txt");
		listing[row.fullPath] = row;
	}
	model.setRows(rowsOf(listing), itemsFolder);

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
			const CFileSystemObjectProperties row = itemProperties(File, QStringLiteral("e%1").arg(i), "txt"); // Sorts above every f
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
		model.setRows(rowsOf(listing), itemsFolder);
		CHECK_FALSE(view.restoreScrollPosition(position));
	}

	SECTION("The position survives the view showing another model in between, as on a tab switch")
	{
		const CFileListView::ScrollPosition position = view.scrollPosition();
		CFileListModel otherModel{ nullptr };
		otherModel.setRows(rowsOf(listing), itemsFolder);
		view.setModel(&otherModel);
		view.scrollToTop();
		view.setModel(&model);

		REQUIRE(view.restoreScrollPosition(position));
		CHECK(yOf("/folder/f150.txt") == y150);
	}
}

// Only the update is timed; building the listings and the model beforehand is not
TEST_CASE("The file list paints its look's row bands, row lines and margin line", "[filelist][view][look]")
{
	static const QColor band{ 255, 0, 0 }, bandLine{ 0, 255, 0 }, rowLine{ 0, 0, 255 }, marginLine{ 255, 0, 255 };
	FileListLook look;
	look.rowSeparatorColor = rowLine;
	look.rowBandColor = band;
	look.rowsPerBand = 2;
	look.rowBandSeparatorColor = bandLine;

	CFileListModel model{ nullptr };
	model.sort(NameColumn, Qt::AscendingOrder);
	CFileListView view;
	view.setModel(&model);
	view.resize(400, 300);
	view.setColumnSeparatorsShown(false);

	const auto show = [&](const int rowCount) {
		std::vector<CFileSystemObject> rows;
		for (int i = 0; i < rowCount; ++i)
			rows.push_back(makeItem(File, QStringLiteral("f%1").arg(i, 3, 10, QChar('0')), "txt"));
		model.setRows(std::move(rows), itemsFolder);
		view.setLook(look);
		view.show();
	};

	QImage image;
	int rowHeight = 0;
	const auto grab = [&] {
		rowHeight = view.visualRect(model.index(0, NameColumn)).height();
		image = view.viewport()->grab().toImage();
	};
	// row: counted from the viewport's top. A row's first line of pixels is above its icon and its text.
	const auto colorAt = [&](const int x, const int row, const int yInRow) {
		return image.pixelColor(QPoint{ x, row * rowHeight + yInRow } * image.devicePixelRatio());
	};
	const int middle = view.viewport()->width() / 2;

	SECTION("Behind the rows, and below the last one")
	{
		show(5);
		grab();
		CHECK(colorAt(middle, 0, 1) != band);
		CHECK(colorAt(middle, 2, 1) == band);
		CHECK(colorAt(middle, 3, 1) == band);
		CHECK(colorAt(middle, 4, 1) != band);
		CHECK(colorAt(middle, 6, 1) == band);

		CHECK(colorAt(middle, 0, rowHeight - 1) == rowLine);
		CHECK(colorAt(middle, 1, rowHeight - 1) == bandLine);
		CHECK(colorAt(middle, 6, rowHeight - 1) == rowLine);
		CHECK(colorAt(middle, 7, rowHeight - 1) == bandLine);
	}

	SECTION("Scrolled along with the rows")
	{
		show(300);
		view.scrollTo(model.index(150, NameColumn), QAbstractItemView::PositionAtTop);
		grab();
		CHECK(colorAt(middle, 0, 1) == band);
		CHECK(colorAt(middle, 1, 1) == band);
		CHECK(colorAt(middle, 2, 1) != band);
	}

	SECTION("The margin line, with the name's editor to its right")
	{
		look.marginLineColor = marginLine;
		show(3);
		grab();

		// Row 5 is below the last row
		int lineX = -1;
		for (int x = 0; x < view.viewport()->width() && lineX < 0; ++x)
		{
			if (colorAt(x, 5, 1) == marginLine)
				lineX = x;
		}
		REQUIRE(lineX > 0);
		CHECK(colorAt(lineX, 0, 1) == marginLine);

		const QModelIndex firstName = model.index(0, NameColumn);
		static_cast<QAbstractItemView&>(view).edit(firstName);
		const QWidget* editor = view.indexWidget(firstName);
		REQUIRE(editor);
		CHECK(editor->geometry().left() > lineX);
	}
}

TEST_CASE("Timings of an update against a reset", "[.][filelist][update][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	static constexpr const char* columnNames[NumberOfColumns] = { "name", "ext", "size", "date" };
	std::printf("%8s %8s %6s %12s %12s\n", "rows", "changes", "sort", "update, ms", "reset, ms");
	for (const int numRows : { 1'000, 10'000, 100'000 })
	{
		std::vector<CFileSystemObjectProperties> baseListing;
		baseListing.reserve((size_t)numRows);
		for (int i = 0; i < numRows; ++i)
		{
			const QString extension = random.randomString(3);
			baseListing.push_back(itemProperties(File, QStringLiteral("file%1").arg(i), extension, random.randomNumber<uint64_t>(0u, 1'000'000u)));
		}
		const std::vector<CFileSystemObject> baseRows(baseListing.cbegin(), baseListing.cend());

		for (const int numChanges : { 30, numRows / 100, numRows / 20, numRows / 10, numRows * 3 / 20, numRows / 4, numRows / 2, numRows * 3 / 4, numRows })
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
				changedListing.push_back(itemProperties(File, QStringLiteral("new%1").arg(i), random.randomString(3)));
			for (int i = 0; i < numChanges / 3; ++i)
				changedListing[random.randomNumber<size_t>(0, changedListing.size() - 1)].size = random.randomNumber<uint64_t>(0u, 1'000'000u);
			const std::vector<CFileSystemObject> changedRows(changedListing.cbegin(), changedListing.cend());

			const auto timeMs = [&](int column, const auto& apply) {
				CFileListModel model{ nullptr };
				model.sort(column, Qt::AscendingOrder);
				model.setRows(baseRows, itemsFolder);
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

			for (const int column : { SizeColumn, NameColumn, ExtColumn })
			{
				const double updateMs = timeMs(column, [](CFileListModel& model, std::vector<CFileSystemObject> rows) { (void)model.updateRows(std::move(rows)); });
				const double resetMs = timeMs(column, [](CFileListModel& model, std::vector<CFileSystemObject> rows) { model.setRows(std::move(rows), itemsFolder); });
				std::printf("%8d %8d %6s %12.1f %12.1f\n", numRows, numChanges, columnNames[column], updateMs, resetMs);
			}
		}
	}
}

// The listing as CPanel::committedListing() copies it out of the panel's map
static std::vector<CFileSystemObject> copyRows(const FileListHashMap& items)
{
	std::vector<CFileSystemObject> rows;
	rows.reserve(items.size());
	for (const auto& [hash, item] : items)
		rows.push_back(item);
	return rows;
}

static double msSince(std::chrono::steady_clock::time_point start)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Min, median and max
static void printSpread(std::vector<double> samples)
{
	std::ranges::sort(samples);
	std::printf(" %7.1f %7.1f %7.1f", samples.front(), samples[samples.size() / 2], samples.back());
}

static CFileSystemObject makeFile(const QString& fullPath, uint64_t size)
{
	CFileSystemObjectProperties properties;
	properties.type = File;
	properties.fullPath = fullPath;
	properties.size = size;
	return CFileSystemObject{ std::move(properties) };
}

// A flattened listing: 100 files per folder, 100 folders per parent, so the name column shows each file's folders
static FileListHashMap flattenedListing(int numRows, CRandomDataGenerator& random)
{
	FileListHashMap listing;
	listing.reserve((size_t)numRows);
	for (int i = 0; i < numRows; ++i)
	{
		const QString path = itemsFolder + QStringLiteral("%1/%2/%3.%4").arg(i / 10'000).arg(i / 100 % 100).arg(random.randomString(12), random.randomString(3));
		CFileSystemObject file = makeFile(path, random.randomNumber<uint64_t>(0u, 1'000'000u));
		listing.try_emplace(file.hash(), std::move(file));
	}
	return listing;
}

static void showRowsInView(CFileListModel& model, QTreeView& view, int sortColumn, const FileListHashMap& items)
{
	model.sort(sortColumn, Qt::AscendingOrder);
	model.setRows(copyRows(items), itemsFolder);
	view.setUniformRowHeights(true);
	view.setModel(&model);
	(void)view.indexAt({ 0, 0 }); // Lays the rows out
}

// The items of itemHashes as target has them, as the panel reports changes committed in place
static void changesToReach(const FileListHashMap& target, const std::vector<qulonglong>& itemHashes, std::vector<CFileSystemObject>& upserted,
	std::vector<qulonglong>& removedHashes)
{
	for (const qulonglong hash : itemHashes)
	{
		if (const auto item = target.find(hash); item != target.end())
			upserted.push_back(item->second);
		else
			removedHashes.push_back(hash);
	}
}

// What the UI thread spends on a flattened listing's refresh that changed one file:
//   copy - the listing copied out of the panel's map, as CPanel::committedListing() does;
//   model - CFileListModel::updateRows() with a view, the GUI's refill;
//   list - SortedFileList::updateRows(), the terminal UI's refill;
//   model and list in place - their applyChanges() given the one changed item, as a change committed in place reaches them.
// Samples alternate between applying the change and taking it back, so each one is a one-file update.
TEST_CASE("Timings of a one-file refresh of a flattened listing", "[.][filelist][update][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	static constexpr int numSamples = 10;
	std::printf("min, median and max of %d samples, ms\n", numSamples);
	std::printf("%7s %8s %5s %23s %23s %23s %23s %23s\n", "rows", "change", "sort", "copy", "model", "list", "model in place", "list in place");
	for (const int numRows : { 10'000, 100'000, 500'000 })
	{
		const FileListHashMap base = flattenedListing(numRows, random);

		const CFileSystemObject& changedFile = std::next(base.begin(), (ptrdiff_t)random.randomNumber<size_t>(0, base.size() - 1))->second;
		const qulonglong changedHash = changedFile.hash();
		FileListHashMap resized = base;
		resized.at(changedHash) = makeFile(changedFile.fullAbsolutePath(), changedFile.size() + 1);

		FileListHashMap added = base;
		CFileSystemObject newFile = makeFile(itemsFolder + QStringLiteral("0/0/new.txt"), 1);
		const qulonglong newFileHash = newFile.hash();
		added.try_emplace(newFileHash, std::move(newFile));

		FileListHashMap removed = base;
		removed.erase(changedHash);

		struct Change
		{
			const char* name;
			const FileListHashMap* listing;
			qulonglong itemHash; // The item it changes
		};
		const Change changes[]{ { "resized", &resized, changedHash }, { "added", &added, newFileHash }, { "removed", &removed, changedHash } };
		const std::pair<int, FileListSortField> sortings[]{ { NameColumn, FileListSortField::Name }, { SizeColumn, FileListSortField::Size } };
		for (const auto& [column, field] : sortings)
		{
			CFileListModel model{ nullptr }, modelInPlace{ nullptr };
			QTreeView view, viewInPlace;
			showRowsInView(model, view, column, base);
			showRowsInView(modelInPlace, viewInPlace, column, base);

			NullSortedFileListObserver observer;
			SortedFileList list{ observer }, listInPlace{ observer };
			for (SortedFileList* sortedList : { &list, &listInPlace })
			{
				sortedList->setSorting(field, Qt::AscendingOrder);
				sortedList->setRows(copyRows(base), itemsFolder);
			}

			for (const auto& [changeName, changed, changedItemHash] : changes)
			{
				std::vector<double> copyMs, modelMs, listMs, modelInPlaceMs, listInPlaceMs;
				for (int sample = 0; sample < numSamples; ++sample)
				{
					const FileListHashMap& target = sample % 2 == 0 ? *changed : base;
					std::vector<CFileSystemObject> upserted;
					std::vector<qulonglong> removedHashes;
					changesToReach(target, { changedItemHash }, upserted, removedHashes);

					auto inPlaceStart = std::chrono::steady_clock::now();
					modelInPlace.applyChanges(upserted, removedHashes);
					(void)viewInPlace.indexAt({ 0, 0 });
					modelInPlaceMs.push_back(msSince(inPlaceStart));

					inPlaceStart = std::chrono::steady_clock::now();
					listInPlace.applyChanges(std::move(upserted), removedHashes);
					listInPlaceMs.push_back(msSince(inPlaceStart));

					auto start = std::chrono::steady_clock::now();
					std::vector<CFileSystemObject> modelRows = copyRows(target);
					copyMs.push_back(msSince(start));
					std::vector<CFileSystemObject> listRows = modelRows;

					start = std::chrono::steady_clock::now();
					const bool modelUpdated = model.updateRows(std::move(modelRows));
					(void)view.indexAt({ 0, 0 });
					modelMs.push_back(msSince(start));

					start = std::chrono::steady_clock::now();
					const bool listUpdated = list.updateRows(std::move(listRows));
					listMs.push_back(msSince(start));

					// A reset would time another path
					REQUIRE(modelUpdated);
					REQUIRE(listUpdated);
				}

				std::printf("%7d %8s %5s", numRows, changeName, field == FileListSortField::Name ? "name" : "size");
				printSpread(copyMs);
				printSpread(modelMs);
				printSpread(listMs);
				printSpread(modelInPlaceMs);
				printSpread(listInPlaceMs);
				std::printf("\n");
			}
		}
	}
}

// What applying a number of changes in place costs, against refilling with every row:
//   list - SortedFileList, the terminal UI's list;
//   model - CFileListModel with a view, the GUI's.
// A refill copies the listing out of the panel's map, then updates the rows, or resets them past SortedFileList::MaxChangesForUpdate.
// The changes are a third each removed, resized and added files, scattered over the folders. Sorted by name.
// Samples alternate between applying the changes and taking them back.
TEST_CASE("Timings of a number of changes applied in place against a refill", "[.][filelist][update][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);
	std::mt19937 shuffler{ g_randomSeed };

	std::printf("Sorted by name; min, median and max of the samples, ms\n");
	std::printf("%7s %7s %23s %23s %23s %23s\n", "rows", "changes", "list in place", "list refill", "model in place", "model refill");
	for (const int numRows : { 100'000, 500'000 })
	{
		const FileListHashMap base = flattenedListing(numRows, random);
		std::vector<qulonglong> shuffledHashes;
		shuffledHashes.reserve(base.size());
		for (const auto& [hash, item] : base)
			shuffledHashes.push_back(hash);
		std::ranges::shuffle(shuffledHashes, shuffler);

		NullSortedFileListObserver observer;
		SortedFileList listInPlace{ observer }, listRefilled{ observer };
		for (SortedFileList* list : { &listInPlace, &listRefilled })
		{
			list->setSorting(FileListSortField::Name, Qt::AscendingOrder);
			list->setRows(copyRows(base), itemsFolder);
		}
		CFileListModel modelInPlace{ nullptr }, modelRefilled{ nullptr };
		QTreeView viewInPlace, viewRefilled;
		showRowsInView(modelInPlace, viewInPlace, NameColumn, base);
		showRowsInView(modelRefilled, viewRefilled, NameColumn, base);

		for (const int numChanges : { 1, 10, 100, 1'000, 5'000, 20'000, numRows / 8 })
		{
			FileListHashMap changed = base;
			std::vector<qulonglong> changedHashes;
			const int third = numChanges / 3;
			for (int i = 0; i < third; ++i)
			{
				const qulonglong removedHash = shuffledHashes[(size_t)i], resizedHash = shuffledHashes[(size_t)(third + i)];
				changed.erase(removedHash);
				CFileSystemObject& resized = changed.at(resizedHash);
				resized = makeFile(resized.fullAbsolutePath(), resized.size() + 1);
				changedHashes.push_back(removedHash);
				changedHashes.push_back(resizedHash);
			}
			for (int i = 2 * third; i < numChanges; ++i)
			{
				const QString path = itemsFolder + QStringLiteral("%1/%2/new%3.txt").arg(random.randomNumber(0, numRows / 10'000 - 1)).arg(random.randomNumber(0, 99)).arg(i);
				CFileSystemObject added = makeFile(path, (uint64_t)i);
				changedHashes.push_back(added.hash());
				changed.try_emplace(added.hash(), std::move(added));
			}

			const int numSamples = numChanges > 5'000 ? 2 : 6;
			std::vector<double> listInPlaceMs, listRefillMs, modelInPlaceMs, modelRefillMs;
			for (int sample = 0; sample < numSamples; ++sample)
			{
				const FileListHashMap& target = sample % 2 == 0 ? changed : base;
				std::vector<CFileSystemObject> upserted;
				std::vector<qulonglong> removedHashes;
				changesToReach(target, changedHashes, upserted, removedHashes);
				std::vector<CFileSystemObject> upsertedForModel = upserted;

				auto start = std::chrono::steady_clock::now();
				listInPlace.applyChanges(std::move(upserted), removedHashes);
				listInPlaceMs.push_back(msSince(start));

				start = std::chrono::steady_clock::now();
				(void)listRefilled.updateRows(copyRows(target));
				listRefillMs.push_back(msSince(start));

				start = std::chrono::steady_clock::now();
				modelInPlace.applyChanges(std::move(upsertedForModel), removedHashes);
				(void)viewInPlace.indexAt({ 0, 0 });
				modelInPlaceMs.push_back(msSince(start));

				start = std::chrono::steady_clock::now();
				(void)modelRefilled.updateRows(copyRows(target));
				(void)viewRefilled.indexAt({ 0, 0 });
				modelRefillMs.push_back(msSince(start));
			}

			std::printf("%7d %7d", numRows, numChanges);
			printSpread(listInPlaceMs);
			printSpread(listRefillMs);
			printSpread(modelInPlaceMs);
			printSpread(modelRefillMs);
			std::printf("\n");
		}
	}
}

// A flattened view's in-place update after folders left its tree, which CPanel runs under the list mutex. Per row count:
// one folder that held no listed item, such as a removed file the listing left out; one folder of 100 files; 100 such folders.
TEST_CASE("Timings of removing the items under folders that left a flattened listing", "[.][filelist][update][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	static constexpr int numSamples = 10;
	std::printf("min, median and max of %d samples, ms\n", numSamples);
	std::printf("%7s %8s %8s %23s\n", "rows", "folders", "removed", "sweep");
	for (const int numRows : { 100'000, 500'000 })
	{
		const FileListHashMap base = flattenedListing(numRows, random);
		std::vector<QString> manyFolders;
		for (int i = 0; i < 100; ++i)
			manyFolders.push_back(itemsFolder + QStringLiteral("0/%1/").arg(i));

		const std::vector<std::vector<QString>> folderSets{ { itemsFolder + QStringLiteral("none/") }, { itemsFolder + QStringLiteral("0/0/") }, manyFolders };
		for (const std::vector<QString>& folders : folderSets)
		{
			std::vector<double> samples;
			size_t removedCount = 0;
			for (int sample = 0; sample < numSamples; ++sample)
			{
				FileListHashMap items = base;
				std::vector<QString> foldersToSweep = folders;
				const auto start = std::chrono::steady_clock::now();
				removedCount = removeItemsUnderFolders(items, std::move(foldersToSweep), FileListHashMap{}).size();
				samples.push_back(msSince(start));
			}

			std::printf("%7d %8zu %8zu", numRows, folders.size(), removedCount);
			printSpread(samples);
			std::printf("\n");
		}
	}
}

// One filter change per keystroke, typing and then erasing. Size sorts by integer, Name and Ext by the collator.
// "dirs" is folders sorted by size: all of size 0, so every comparison is a tie.
TEST_CASE("Timings of a name filter change", "[.][filelist][filter][timing]")
{
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	const std::vector<QString> keystrokes{ "a", "ab", "abc", "ab", "a", "" };
	const auto msSince = [](std::chrono::steady_clock::time_point start) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	};

	std::printf("%8s %6s %8s %8s %12s %12s\n", "rows", "sort", "filter", "shown", "model, ms", "+ view, ms");
	for (const int numRows : { 1'000, 10'000, 100'000 })
	{
		std::vector<CFileSystemObject> files, folders;
		files.reserve((size_t)numRows);
		folders.reserve((size_t)numRows);
		for (int i = 0; i < numRows; ++i)
		{
			const QString name = random.randomString(12), extension = random.randomString(3);
			files.emplace_back(itemProperties(File, name, extension, random.randomNumber<uint64_t>(0u, 1'000'000u)));
			folders.emplace_back(itemProperties(Directory, random.randomString(12)));
		}

		struct Configuration {
			const char* name;
			int column;
			const std::vector<CFileSystemObject>& rows;
		};
		for (const Configuration& configuration : { Configuration{ "size", SizeColumn, files }, Configuration{ "name", NameColumn, files },
			Configuration{ "ext", ExtColumn, files }, Configuration{ "dirs", SizeColumn, folders } })
		{
			const int column = configuration.column;
			CFileListModel model{ nullptr };
			model.sort(column, Qt::AscendingOrder);
			model.setRows(configuration.rows, itemsFolder);

			CFileListModel viewedModel{ nullptr };
			viewedModel.sort(column, Qt::AscendingOrder);
			viewedModel.setRows(configuration.rows, itemsFolder);
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
				std::printf("%8d %6s %8s %8d %12.1f %12.1f\n", numRows, configuration.name, filter.isEmpty() ? "(none)" : qUtf8Printable(filter), model.rowCount(), modelMs, viewMs);
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

TEST_CASE("NaturalSort::compare sorts an empty name first by key", "[filelist][collator]")
{
	const QCollatorSortKey empty = NaturalSort::sortKey(QString{}), nonEmpty = NaturalSort::sortKey(QStringLiteral("a"));
	CHECK(NaturalSort::compare(empty, nonEmpty) < 0);
	CHECK(NaturalSort::compare(nonEmpty, empty) > 0);
	CHECK(NaturalSort::compare(empty, NaturalSort::sortKey(QString{})) == 0);
}

// Mixed case, digits and an empty name exercise the collator's case, numeric and empty-string rules.
// Not on macOS: the two orders differ there, see NaturalSort::compare() of keys.
#ifndef __APPLE__
TEST_CASE("NaturalSort::compare orders the same by key as by string", "[filelist][collator]")
{
	INFO("Random seed: " << g_randomSeed);
	CRandomDataGenerator random;
	random.setSeed(g_randomSeed);

	std::vector<QString> names;
	for (int i = 0; i < 10'000; ++i)
	{
		const QString prefix = random.randomString(2), number = QString::number(random.randomNumber<int>(0, 2000));
		const QString name = prefix + number + random.randomString(2);
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
	std::stable_sort(byKey.begin(), byKey.end(), [&](uint32_t l, uint32_t r) { return NaturalSort::compare(keys[l], keys[r]) < 0; });

	const auto [byKeyIt, byCompareIt] = std::mismatch(byKey.cbegin(), byKey.cend(), byCompare.cbegin());
	if (byKeyIt != byKey.cend())
	{
		const uint32_t l = *byKeyIt, r = *byCompareIt;
		INFO("First difference at " << byKeyIt - byKey.cbegin() << ": \"" << names[l].toStdString() << "\" by key, \"" << names[r].toStdString() << "\" by string");
		INFO("Comparing the first to the second: " << NaturalSort::compare(names[l], names[r]) << " by string, " << NaturalSort::compare(keys[l], keys[r]) << " by key");
		FAIL_CHECK("The two orders differ");
	}
}
#endif

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
			rows.emplace_back(itemProperties(File, stems[i], extensions[i % 20]));

		CFileListModel model{ nullptr };
		model.sort(ExtColumn, Qt::AscendingOrder);
		const size_t before = processPrivateBytes();
		const auto start = std::chrono::steady_clock::now();
		model.setRows(std::move(rows), itemsFolder);
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
		{
			const QString stem = random.randomString(12);
			names.push_back(stem + '.' + random.randomString(3));
		}

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
				std::sort(sortedByKey.begin(), sortedByKey.end(), [&](uint32_t l, uint32_t r) { return NaturalSort::compare(keys[l], keys[r]) < 0; });
				const double sortByKeyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

				std::printf("%8d %6d %8zu %10.1f %10.1f\n", numNames, round, sorted.size(), sortMs, sortByKeyMs);
			}
		}
	}
}

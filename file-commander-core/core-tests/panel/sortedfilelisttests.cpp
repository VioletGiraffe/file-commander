#include "cfilesystemobject.h"
#include "filelist/filelistsorter.h"
#include "filelist/filenamefilter.h"
#include "filelist/sortedfilelist.h"

#include "filelisttestitems.h"

#include "qt_helpers.hpp" // operator<< for QString, so Catch2 can print a name in a failure report


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <random>
#include <stdint.h>
#include <vector>

namespace {


// What a row shows: its path, size and time
QString rowText(const CFileSystemObject& row)
{
	return row.fullAbsolutePath() + ' ' + QString::number(row.size()) + ' ' + QString::number(row.modificationTime());
}

QStringList rowTexts(const SortedFileList& list)
{
	QStringList texts;
	for (int row = 0; row < list.rowCount(); ++row)
		texts.push_back(rowText(list.rowAt(row)));
	return texts;
}

QStringList pathsOf(const QStringList& texts)
{
	QStringList paths;
	for (const QString& text : texts)
		paths.push_back(text.section(' ', 0, 0));
	return paths;
}

// Keeps its own copy of the rows, changed only as the notifications say, and checks it against the list around every change.
// Records the kind of each change.
class ReplayingObserver final : public SortedFileListObserver
{
public:
	const SortedFileList* list = nullptr;
	QStringList copy;
	QStringList changes;

	void beforeReset() override { changes.push_back(QStringLiteral("reset")); checkPositions(); }
	void afterReset() override { copy = rowTexts(*list); }

	void beforeInsert(int first, int last) override
	{
		changes.push_back(QStringLiteral("insert"));
		checkPositions();
		_first = first;
		_last = last;
	}

	void afterInsert() override
	{
		for (int row = _first; row <= _last; ++row)
			copy.insert(row, rowText(list->rowAt(row)));
		checkPositions();
	}

	void beforeRemove(int first, int last) override
	{
		changes.push_back(QStringLiteral("remove"));
		checkPositions();
		_first = first;
		_last = last;
	}

	void afterRemove() override
	{
		copy.remove(_first, _last - _first + 1);
		checkPositions();
	}

	void beforeMove(int row, int destination) override
	{
		changes.push_back(QStringLiteral("move"));
		checkPositions();
		REQUIRE((destination < row || destination > row + 1));
		_first = row;
		_last = destination;
	}

	void afterMove() override
	{
		copy.move(_first, _last > _first ? _last - 1 : _last);
		checkPositions();
	}

	void rowChanged(int row) override
	{
		changes.push_back(QStringLiteral("change"));
		REQUIRE((row >= 0 && row < copy.size()));
		copy[row] = rowText(list->rowAt(row));
		checkPositions();
	}

	void beforeReorder(SortedFileListReorderCause cause) override
	{
		switch (cause)
		{
		case SortedFileListReorderCause::Sort: changes.push_back(QStringLiteral("sort")); break;
		case SortedFileListReorderCause::Filter: changes.push_back(QStringLiteral("filter")); break;
		case SortedFileListReorderCause::Addition: changes.push_back(QStringLiteral("addition")); break;
		}
		checkPositions();
	}

	void afterReorder(SortedFileListReorderCause) override { copy = rowTexts(*list); }

	// Every row's data too: a changed row's data reaches the copy only through rowChanged()
	void checkRows() const { CHECK(copy == rowTexts(*list)); }

private:
	// Data aside: updateRows() changes a row's data before it notifies the row's move
	void checkPositions() const { CHECK(pathsOf(copy) == pathsOf(rowTexts(*list))); }

	int _first = 0, _last = 0;
};

struct ObservedList
{
	ObservedList() { observer.list = &list; }

	ReplayingObserver observer;
	SortedFileList list{ observer };
};

// The rows the list must show, worked out without it
QStringList expectedRowTexts(std::vector<CFileSystemObject> items, const QString& filterText, FileListSortField field, Qt::SortOrder order)
{
	const FileNameFilter filter{ splitNameFilterField(filterText), Qt::CaseInsensitive };
	std::erase_if(items, [&](const CFileSystemObject& item) { return !filter.matches(item.fullName()); });

	FileListSorter sorter;
	sorter.setSorting(field, order);
	sorter.setListedFolder(itemsFolder);
	sorter.sort(items);

	QStringList texts;
	for (const CFileSystemObject& item : items)
		texts.push_back(rowText(item));
	return texts;
}

} // namespace

TEST_CASE("Sorted file list - random updates, filters and sortings are notified as they happen", "[sortedfilelist]")
{
	static const FileListSortField fields[] = { FileListSortField::Name, FileListSortField::Extension, FileListSortField::Size, FileListSortField::Date };
	static const QString filters[] = { QString{}, QStringLiteral("a"), QStringLiteral("1;b") };

	for (uint32_t seed = 1; seed <= 30; ++seed)
	{
		INFO("Seed " << seed);
		std::mt19937 random{ seed };
		const auto randomBelow = [&random](int n) { return std::uniform_int_distribution<int>{ 0, n - 1 }(random); };

		// 30 names, so that most rounds keep, change, add and drop rows
		QStringList names;
		for (int i = 0; i < 30; ++i)
			names.push_back(QStringLiteral("%1%2").arg(QChar(u'a' + i % 6)).arg(i));

		const auto randomItem = [&](const QString& name) {
			return makeItem(randomBelow(5) == 0 ? Directory : File, name, randomBelow(2) ? QStringLiteral("txt") : QStringLiteral("doc"), (uint64_t)randomBelow(4), randomBelow(4));
		};
		const auto randomItems = [&] {
			std::vector<CFileSystemObject> items;
			for (const QString& name : names)
			{
				if (randomBelow(10) < 7)
					items.push_back(randomItem(name));
			}
			return items;
		};

		ObservedList o;
		std::vector<CFileSystemObject> items = randomItems();
		QString filterText;
		FileListSortField field = FileListSortField::Name;
		Qt::SortOrder order = Qt::AscendingOrder;
		o.list.setRows(items, itemsFolder);

		for (int round = 0; round < 40; ++round)
		{
			INFO("Round " << round);
			switch (randomBelow(6))
			{
			case 0:
				field = fields[randomBelow(4)];
				order = randomBelow(2) ? Qt::AscendingOrder : Qt::DescendingOrder;
				o.list.setSorting(field, order);
				break;
			case 1:
				filterText = filters[randomBelow(3)];
				o.list.setNameFilter(filterText);
				break;
			case 2:
			{
				std::vector<CFileSystemObject> added;
				for (const QString& name : names)
				{
					const CFileSystemObject item = randomItem(name);
					if (randomBelow(3) == 0 && std::ranges::none_of(items, [&item](const CFileSystemObject& listed) { return listed.fullAbsolutePath() == item.fullAbsolutePath(); }))
						added.push_back(item);
				}
				items.insert(items.end(), added.cbegin(), added.cend());
				o.list.addRows(std::move(added));
				break;
			}
			default:
				items = randomItems();
				REQUIRE(o.list.updateRows(items));
				break;
			}

			o.observer.checkRows();
			CHECK(o.observer.copy == expectedRowTexts(items, filterText, field, order));
		}
	}
}

TEST_CASE("Sorted file list - updateRows() resets past MaxChangesForUpdate changed rows", "[sortedfilelist]")
{
	const auto items = [](size_t count) {
		std::vector<CFileSystemObject> result;
		for (size_t i = 0; i < count; ++i)
			result.push_back(makeItem(File, QString::number(i), "txt"));
		return result;
	};

	ObservedList o;
	o.list.setRows({}, itemsFolder);
	o.observer.changes.clear();

	CHECK(o.list.updateRows(items(SortedFileList::MaxChangesForUpdate)));
	CHECK(o.observer.changes == QStringList{ "insert" });
	o.observer.checkRows();

	o.list.setRows({}, itemsFolder);
	o.observer.changes.clear();

	CHECK_FALSE(o.list.updateRows(items(SortedFileList::MaxChangesForUpdate + 1)));
	CHECK(o.observer.changes == QStringList{ "reset" });
	o.observer.checkRows();
}

TEST_CASE("Sorted file list - the same sorting or filter again changes nothing", "[sortedfilelist]")
{
	ObservedList o;
	o.list.setRows({ makeItem(File, "b", "txt"), makeItem(File, "a", "txt") }, itemsFolder);
	o.list.setSorting(FileListSortField::Name, Qt::DescendingOrder);
	o.list.setNameFilter(QStringLiteral("a"));
	o.observer.changes.clear();

	o.list.setSorting(FileListSortField::Name, Qt::DescendingOrder);
	o.list.setNameFilter(QStringLiteral("a"));
	CHECK(o.observer.changes.isEmpty());
}

TEST_CASE("Sorted file list - addRows() of rows the filter hides changes no row, and the totals count them", "[sortedfilelist]")
{
	ObservedList o;
	o.list.setRows({ makeItem(File, "a", "txt") }, itemsFolder);
	o.list.setNameFilter(QStringLiteral("a"));
	o.observer.changes.clear();

	o.list.addRows({ makeItem(File, "b", "txt"), makeItem(File, "c", "txt") });
	CHECK(o.observer.changes.isEmpty());
	CHECK(o.list.rowCount() == 1);
	CHECK(o.list.contentsSummary().numFiles == 3);

	o.list.addRows({ makeItem(File, "ba", "txt"), makeItem(File, "d", "txt") });
	CHECK(o.observer.changes == QStringList{ "addition" });
	o.observer.checkRows();
	CHECK(o.list.rowCount() == 2);
}

TEST_CASE("Sorted file list - lookups skip the rows the filter hides; the totals count them", "[sortedfilelist]")
{
	ObservedList o;
	const CFileSystemObject hidden = makeItem(File, "b", "txt");
	o.list.setRows({ makeItem(File, "a", "txt"), hidden, makeItem(Directory, "x") }, itemsFolder);
	o.list.setNameFilter(QStringLiteral("a"));

	CHECK(o.list.rowCount() == 1);
	CHECK(o.list.rowOf(hidden.hash()) == -1);
	CHECK(o.list.firstFileRow() == 0);
	CHECK(o.list.contentsSummary().numFiles == 2);
	CHECK(o.list.contentsSummary().numFolders == 1);
}

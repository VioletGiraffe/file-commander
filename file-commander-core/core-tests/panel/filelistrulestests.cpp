#include "cfilesystemobject.h"
#include "filelist/filelistsorter.h"
#include "filelist/filenamefilter.h"
#include "filelist/filetimetext.h"
#include "filelist/foldercontentssummary.h"
#include "filesystemhelperfunctions.h"

#include "qt_helpers.hpp" // operator<< for QString, so Catch2 can print a name in a failure report


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QDateTime>
#include <QLocale>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <thread>
#include <time.h>
#include <vector>

// An entry of /folder/, built in memory
static CFileSystemObject makeItem(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	CFileSystemObjectProperties properties;
	properties.type = type;
	const QString fullName = extension.isEmpty() ? name : name + '.' + extension;
	properties.fullPath = QStringLiteral("/folder/") + fullName + (type == Directory ? QStringLiteral("/") : QString{});
	properties.size = size;
	properties.modificationTime = modificationTime;
	return CFileSystemObject{ properties };
}

static CFileSystemObject makeCdUpItem()
{
	CFileSystemObjectProperties properties;
	properties.type = Directory;
	properties.fullPath = QStringLiteral("/");
	properties.isCdUp = true;
	return CFileSystemObject{ properties };
}

// An entry anywhere, built in memory. A folder's path ends with '/'.
static CFileSystemObject makeItemAt(FileSystemObjectType type, const QString& fullPath)
{
	CFileSystemObjectProperties properties;
	properties.type = type;
	properties.fullPath = fullPath;
	return CFileSystemObject{ properties };
}

static QStringList sortedNames(std::vector<CFileSystemObject> items, FileListSortField field, Qt::SortOrder order)
{
	FileListSorter sorter;
	sorter.setSorting(field, order);
	sorter.sort(items);

	QStringList names;
	for (const CFileSystemObject& item : items)
		names.push_back(item.fullName().toString());
	return names;
}

TEST_CASE("File list - [..] sorts first, then folders, then files, in either direction", "[filelistrules][sort]")
{
	const std::vector<CFileSystemObject> items{ makeItem(File, "z", "txt"), makeItem(Directory, "b"), makeCdUpItem(), makeItem(File, "a", "txt"), makeItem(Directory, "a"), makeItem(File, "m", "doc") };

	CHECK(sortedNames(items, FileListSortField::Name, Qt::AscendingOrder) == QStringList{ "..", "a", "b", "a.txt", "m.doc", "z.txt" });
	CHECK(sortedNames(items, FileListSortField::Name, Qt::DescendingOrder) == QStringList{ "..", "b", "a", "z.txt", "m.doc", "a.txt" });
}

TEST_CASE("File list - each sort field sorts by its own key", "[filelistrules][sort]")
{
	SECTION("Name, numbers by value")
	{
		const std::vector<CFileSystemObject> items{ makeItem(File, "file10"), makeItem(File, "file2"), makeItem(File, "file1") };
		CHECK(sortedNames(items, FileListSortField::Name, Qt::AscendingOrder) == QStringList{ "file1", "file2", "file10" });
	}

	SECTION("Extension, then name; no extension sorts first")
	{
		const std::vector<CFileSystemObject> items{ makeItem(File, "b", "txt"), makeItem(File, "c", "doc"), makeItem(File, "Makefile"), makeItem(File, "a", "doc") };
		CHECK(sortedNames(items, FileListSortField::Extension, Qt::AscendingOrder) == QStringList{ "Makefile", "a.doc", "c.doc", "b.txt" });
	}

	SECTION("Folders by name in the extension column")
	{
		const std::vector<CFileSystemObject> items{ makeItem(Directory, "b"), makeItem(Directory, "a"), makeItem(File, "z", "doc") };
		CHECK(sortedNames(items, FileListSortField::Extension, Qt::AscendingOrder) == QStringList{ "a", "b", "z.doc" });
	}

	SECTION("Size; equal sizes by name, then extension")
	{
		const std::vector<CFileSystemObject> items{ makeItem(File, "big", "", 30u), makeItem(File, "file10", "", 5u), makeItem(File, "small", "", 10u),
			makeItem(File, "file9", "txt", 5u), makeItem(File, "file9", "", 5u) };
		CHECK(sortedNames(items, FileListSortField::Size, Qt::AscendingOrder) == QStringList{ "file9", "file9.txt", "file10", "small", "big" });
		CHECK(sortedNames(items, FileListSortField::Size, Qt::DescendingOrder) == QStringList{ "big", "small", "file10", "file9.txt", "file9" });
	}

	SECTION("Folders of equal size by name")
	{
		const std::vector<CFileSystemObject> items{ makeItem(Directory, "b10"), makeItem(Directory, "a"), makeItem(Directory, "b9") };
		CHECK(sortedNames(items, FileListSortField::Size, Qt::AscendingOrder) == QStringList{ "a", "b9", "b10" });
	}

	SECTION("Modification time")
	{
		const std::vector<CFileSystemObject> items{ makeItem(File, "new", "", 0u, 300), makeItem(File, "old", "", 0u, 100), makeItem(File, "mid", "", 0u, 200) };
		CHECK(sortedNames(items, FileListSortField::Date, Qt::AscendingOrder) == QStringList{ "old", "mid", "new" });
	}
}

TEST_CASE("File list - numbers in names sort by value in the C locale too", "[filelistrules][sort]")
{
	const QLocale previousLocale;
	QLocale::setDefault(QLocale::c());

	// The collators are built once per thread, so only a new thread builds them under the C locale
	QStringList names;
	std::thread{ [&names] {
		names = sortedNames({ makeItem(File, "file10"), makeItem(File, "file2"), makeItem(File, "file1") }, FileListSortField::Name, Qt::AscendingOrder);
	} }.join();

	QLocale::setDefault(previousLocale);
	CHECK(names == QStringList{ "file1", "file2", "file10" });
}

// Only Windows splits a leading dot off as an extension
#ifdef _WIN32
TEST_CASE("File list - a file with nothing before its extension displays and sorts by its full name", "[filelistrules]")
{
	CHECK(fileListDisplayName(makeItem(File, "", "jpg")) == QStringLiteral(".jpg"));
	CHECK(fileListDisplayName(makeItem(File, "notes", "txt")) == QStringLiteral("notes"));
	CHECK(sortedNames({ makeItem(File, "b"), makeItem(File, "", "a") }, FileListSortField::Name, Qt::AscendingOrder) == QStringList{ ".a", "b" });
}
#endif

TEST_CASE("File list - a row's folder prefix is its folder relative to the listed one", "[filelistrules]")
{
	const QString listedFolder = QStringLiteral("/folder/");
	CHECK(fileListFolderPrefix(makeItem(File, "a", "txt"), listedFolder).isEmpty());
	CHECK(fileListFolderPrefix(makeItem(Directory, "sub"), listedFolder).isEmpty());
	CHECK(fileListFolderPrefix(makeCdUpItem(), listedFolder).isEmpty());
	CHECK(fileListFolderPrefix(makeItemAt(File, QStringLiteral("/folder/x/y/a.txt")), listedFolder) == toNativeSeparators(QStringLiteral("x/y/")));
	CHECK(fileListFolderPrefix(makeItemAt(Directory, QStringLiteral("/folder/x/sub/")), listedFolder) == toNativeSeparators(QStringLiteral("x/")));
	CHECK(fileListFolderPrefix(makeItemAt(File, QStringLiteral("/other/a.txt")), listedFolder) == toNativeSeparators(QStringLiteral("/other/")));
}

TEST_CASE("File list - sorting by name takes the folder prefix first", "[filelistrules][sort]")
{
	std::vector<CFileSystemObject> items{ makeItemAt(File, QStringLiteral("/folder/b/a.txt")), makeItemAt(File, QStringLiteral("/folder/m.txt")),
		makeItemAt(File, QStringLiteral("/folder/a/z.txt")) };

	FileListSorter sorter;
	sorter.setSorting(FileListSortField::Name, Qt::AscendingOrder);
	sorter.setListedFolder(QStringLiteral("/folder/"));
	sorter.sort(items);

	QStringList paths;
	for (const CFileSystemObject& item : items)
		paths.push_back(item.fullAbsolutePath());
	CHECK(paths == QStringList{ "/folder/a/z.txt", "/folder/b/a.txt", "/folder/m.txt" });
}

TEST_CASE("File list - a file's short time shows the year only for other years, the long one always and to the second", "[filelistrules]")
{
	const int year = QDate::currentDate().year();
	const time_t thisYear = QDateTime{ QDate{ year, 1, 2 }, QTime{ 3, 4, 5 } }.toSecsSinceEpoch();
	const time_t lastYear = QDateTime{ QDate{ year - 1, 12, 31 }, QTime{ 23, 58, 59 } }.toSecsSinceEpoch();
	const QString twoDigitYear = QString::number(year % 100).rightJustified(2, '0');
	const QString lastTwoDigitYear = QString::number((year - 1) % 100).rightJustified(2, '0');

	CHECK(fileTimeText(thisYear, FileTimeTextLength::Short) == QStringLiteral("02 Jan 03:04"));
	CHECK(fileTimeText(lastYear, FileTimeTextLength::Short) == QStringLiteral("31 Dec '") + lastTwoDigitYear);
	CHECK(fileTimeText(thisYear, FileTimeTextLength::Short).size() == maxShortFileTimeTextLength);
	CHECK(fileTimeText(thisYear, FileTimeTextLength::Long) == QStringLiteral("02 Jan '%1 03:04:05").arg(twoDigitYear));
	CHECK(fileTimeText(lastYear, FileTimeTextLength::Long) == QStringLiteral("31 Dec '%1 23:58:59").arg(lastTwoDigitYear));

	const QLocale previousLocale;
	QLocale::setDefault(QLocale{ QLocale::German });
	CHECK(fileTimeText(lastYear, FileTimeTextLength::Short) == QStringLiteral("31 Dec '") + lastTwoDigitYear); // German: "Dez"
	QLocale::setDefault(previousLocale);
}

TEST_CASE("File list - the name filter language", "[filelistrules][filter]")
{
	const auto matches = [](const QString& fieldText, const QString& name, Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive) {
		return FileNameFilter{ splitNameFilterField(fieldText), caseSensitivity }.matches(name);
	};

	SECTION("An unanchored filter matches anywhere in the name")
	{
		CHECK(matches("doc", "docs"));
		CHECK(matches("doc", "c.doc"));
		CHECK_FALSE(matches("doc", "a.txt"));
	}

	SECTION("'^' and '$' anchor the ends, and are literal elsewhere")
	{
		CHECK(matches("^notes", "notes.txt"));
		CHECK_FALSE(matches("^notes", "my notes.txt"));
		CHECK(matches("txt$", "notes.txt"));
		CHECK_FALSE(matches("txt$", "notes.txt.bak"));
		CHECK(matches("^notes.txt$", "notes.txt"));
		CHECK_FALSE(matches("^notes.txt$", "notes.txt2"));
		CHECK(matches("a$b", "xa$by"));
	}

	SECTION("'*' is any run of characters, '?' exactly one")
	{
		CHECK(matches("log?.txt", "log1.txt"));
		CHECK_FALSE(matches("log?.txt", "log10.txt"));
		CHECK(matches("^log*.txt$", "log10.txt"));
		CHECK(matches("a*b", QStringLiteral("a\nb"))); // A POSIX name may contain a newline
	}

	SECTION("Everything else is literal")
	{
		CHECK(matches("[ab]", "x[ab]y"));
		CHECK_FALSE(matches("[ab]", "a"));
		CHECK_FALSE(matches("a.c", "abc"));
	}

	SECTION("Case")
	{
		CHECK(matches("TXT", "a.txt"));
		CHECK_FALSE(matches("TXT", "a.txt", Qt::CaseSensitive));
	}

	SECTION("';' separates alternatives")
	{
		CHECK(matches("*.txt;*.doc", "a.txt"));
		CHECK(matches("*.txt;*.doc", "c.doc"));
		CHECK_FALSE(matches("*.txt;*.doc", "b.png"));
	}

	SECTION("No filter, or '*' among them, matches every name")
	{
		CHECK(FileNameFilter{}.matches(QStringLiteral("a.png")));
		CHECK(matches({}, "a.png"));
		CHECK(matches(";", "a.png"));
		CHECK(matches("*.txt;*", "a.png"));
	}
}

TEST_CASE("File list - the contents summary counts files, folders and their size, but not [..]", "[filelistrules]")
{
	FolderContentsSummary summary;
	for (const CFileSystemObject& item : { makeCdUpItem(), makeItem(File, "a", "txt", 3u), makeItem(File, "b", "", 4u), makeItem(Directory, "c") })
		summary.add(item);

	CHECK(summary.numFiles == 2);
	CHECK(summary.numFolders == 1);
	CHECK(summary.size == 7);
}

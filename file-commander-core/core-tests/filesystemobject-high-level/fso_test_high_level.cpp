#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QtTest>
RESTORE_COMPILER_WARNINGS

#include <iostream>

class FileSystemObjectTestHighLevel : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void testPathHierarchy();
	void cleanupTestCase();
};

void FileSystemObjectTestHighLevel::initTestCase()
{

}

void FileSystemObjectTestHighLevel::testPathHierarchy()
{
	QVERIFY(CFileSystemObject::pathHierarchy("").empty());

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy(".");
		QVERIFY(hierarchy.size() == 1 && hierarchy.front() == ".");
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("..");
		QVERIFY(hierarchy.size() == 1 && hierarchy.front() == "..");
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("/");
		QVERIFY(hierarchy.size() == 1 && hierarchy.front() == "");
	}

#ifndef _WIN32
	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("/Users/admin/Downloads/1.txt");
		const std::vector<QString> reference{"/Users/admin/Downloads/1.txt", "/Users/admin/Downloads", "/Users/admin", "/Users"};
		QCOMPARE(hierarchy, reference);
	}
#else
	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:/Docs/1/2/3/txt.files/important.document.txt");
		const std::vector<QString> reference{ "R:/Docs/1/2/3/txt.files/important.document.txt", "R:/Docs/1/2/3/txt.files", "R:/Docs/1/2/3", "R:/Docs/1/2", "R:/Docs/1", "R:/Docs", "R:/"};
		QCOMPARE(hierarchy, reference);
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:");
		const std::vector<QString> reference{ "R:" };
		QCOMPARE(hierarchy, reference);
	}

	{
		const auto hierarchy = CFileSystemObject::pathHierarchy("R:/");
		const std::vector<QString> reference{ "R:/" };
		QCOMPARE(hierarchy, reference);
	}
#endif
}

void FileSystemObjectTestHighLevel::cleanupTestCase()
{

}

DISABLE_COMPILER_WARNINGS
QTEST_APPLESS_MAIN(FileSystemObjectTestHighLevel)
#include "fso_test_high_level.moc"
RESTORE_COMPILER_WARNINGS

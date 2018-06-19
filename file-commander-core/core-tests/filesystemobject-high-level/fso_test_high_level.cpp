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
}

void FileSystemObjectTestHighLevel::cleanupTestCase()
{

}

DISABLE_COMPILER_WARNINGS
QTEST_APPLESS_MAIN(FileSystemObjectTestHighLevel)
#include "fso_test_high_level.moc"
RESTORE_COMPILER_WARNINGS

#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QtTest>
RESTORE_COMPILER_WARNINGS

#include <iostream>

class FileSystemObjectTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void test();
	void cleanupTestCase();
};

void FileSystemObjectTest::initTestCase()
{

}

void FileSystemObjectTest::test()
{
	QVERIFY(true);
}

void FileSystemObjectTest::cleanupTestCase()
{

}

DISABLE_COMPILER_WARNINGS
QTEST_APPLESS_MAIN(FileSystemObjectTest)
#include "fso_test.moc"
RESTORE_COMPILER_WARNINGS

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
	void testEmptyObject();
	void cleanupTestCase();
};

void FileSystemObjectTest::initTestCase()
{

}

void FileSystemObjectTest::testEmptyObject()
{
	QFileInfo_Test info;
	CFileSystemObject fso((info));

	QVERIFY(fso == CFileSystemObject());
	QVERIFY(!fso.exists());
	QVERIFY(fso.extension() == "");
	QVERIFY(fso.fullAbsolutePath() == "");
	QVERIFY(fso.fullName() == "");
	QVERIFY(fso.hash() == 0);
	QVERIFY(fso.isCdUp() == false);
	QVERIFY(fso.isChildOf(CFileSystemObject()) == false);
	QVERIFY(fso.isDir() == false);
	QVERIFY(fso.isEmptyDir() == false);
	QVERIFY(fso.isExecutable() == false);
	QVERIFY(fso.isFile() == false);
	QVERIFY(fso.isHidden() == false);
	QVERIFY(fso.isMovableTo(CFileSystemObject()) == false);
	QVERIFY(fso.isNetworkObject() == false);
	QVERIFY(fso.isReadable() == false);
	QVERIFY(fso.isValid() == false);
	QVERIFY(fso.isWriteable() == false);
	QVERIFY(fso.name() == false);
	QVERIFY(fso.parentDirPath() == "");
	QVERIFY(fso.size() == 0);
	QVERIFY(fso.type() == UnknownType);
}

void FileSystemObjectTest::cleanupTestCase()
{

}

DISABLE_COMPILER_WARNINGS
QTEST_APPLESS_MAIN(FileSystemObjectTest)
#include "fso_test.moc"
RESTORE_COMPILER_WARNINGS

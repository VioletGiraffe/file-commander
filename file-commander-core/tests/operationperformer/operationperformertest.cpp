#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QtTest>
RESTORE_COMPILER_WARNINGS

class TestOperationPerformer : public QObject
{
	Q_OBJECT

private slots:
	void testCopy();
};

void TestOperationPerformer::testCopy()
{
	const QString srcDirPath = "E:\\Development\\Projects\\Personal\\file-commander\\file-commander-core\\tests\\operationperformer\\test_folder\\";
	const QString destDirPath = "E:/Development/Projects/Personal/file-commander/bin/copy-move-test-folder/";
#ifdef _WIN32
	std::system((QString("rmdir /S /Q ") % '\"' % QString(destDirPath).replace('/', '\\') % '\"').toUtf8().data());
#endif
	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(srcDirPath)}, destDirPath);
	p.start();
	while (!p.done());

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(srcDirPath, sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath, destTree);

	QVERIFY(sourceTree == destTree);
}

DISABLE_COMPILER_WARNINGS

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"

RESTORE_COMPILER_WARNINGS
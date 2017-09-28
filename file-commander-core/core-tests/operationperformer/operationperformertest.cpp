#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "container/set_operations.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QtTest>
RESTORE_COMPILER_WARNINGS

#include <iostream>

class TestOperationPerformer : public QObject
{
	Q_OBJECT

private slots:
	void fileSystemObjectTest();
	void testCopy();
};

inline bool compareFolderContents(const std::vector<CFileSystemObject>& source, const std::vector<CFileSystemObject>& dest)
{
	QStringList pathsSource;
	for (const auto& item : source)
		pathsSource.push_back(item.fullAbsolutePath());

	const auto longestCommonPrefixL = SetOperations::longestCommonStart(pathsSource);

	QStringList pathsDest;
	for (const auto& item : dest)
		pathsDest.push_back(item.fullAbsolutePath());

	const auto longestCommonPrefixR = SetOperations::longestCommonStart(pathsDest);

	for (auto& path : pathsSource)
		path = path.mid(longestCommonPrefixL.length());

	for (auto& path : pathsDest)
		path = path.mid(longestCommonPrefixR.length());

	bool differenceDetected = false;

	const auto diff = SetOperations::calculateDiff(pathsSource, pathsDest);
	if (!diff.elements_from_a_not_in_b.empty())
	{
		differenceDetected = true;
		qDebug() << "Items from source not in dest:";
		for (const auto& item : diff.elements_from_a_not_in_b)
			qDebug() << item;
	}

	if (!diff.elements_from_b_not_in_a.empty())
	{
		differenceDetected = true;
		qDebug() << "Items from dest not in source:";
		for (const auto& item : diff.elements_from_b_not_in_a)
			qDebug() << item;
	}

	return !differenceDetected;
}

inline QString srcTestDirPath()
{
	return QApplication::applicationDirPath() + "/../../file-commander-core/core-tests/operationperformer/test_folder/";
}

inline QString dstTestDirPath()
{
	return QApplication::applicationDirPath() + "/copy-move-test-folder/";
}

void TestOperationPerformer::fileSystemObjectTest()
{
	CFileSystemObject o(srcTestDirPath());
	QVERIFY(o.fullAbsolutePath() != o.parentDirPath() + '/');
}

void TestOperationPerformer::testCopy()
{
	// TODO: remove hard-coded paths
	const QString srcDirPath = srcTestDirPath();
	const QString destDirPath = dstTestDirPath();

	qDebug() << "Source:" << srcDirPath;
	qDebug() << "Dest:" << destDirPath;

	// TODO: extract this into init() / cleanup()
#ifdef _WIN32
	std::system((QString("rmdir /S /Q ") % '\"' % QString(destDirPath).replace('/', '\\') % '\"').toUtf8().data());
#else
#error cleanup script not defined
#endif
	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(srcDirPath)}, destDirPath);
	p.start();
	while (!p.done());

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(srcDirPath, sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath + CFileSystemObject(srcTestDirPath()).fullName(), destTree);

	QVERIFY(compareFolderContents(sourceTree, destTree));
}

DISABLE_COMPILER_WARNINGS

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"

RESTORE_COMPILER_WARNINGS
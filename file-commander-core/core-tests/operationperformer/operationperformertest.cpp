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
	void testCopy();
};

inline void printFolderComparison(const std::vector<CFileSystemObject>& l, const std::vector<CFileSystemObject>& r)
{
	QStringList pathsL;
	for (const auto& item : l)
		pathsL.push_back(item.fullAbsolutePath());

	const auto longestCommonPrefixL = SetOperations::longestCommonStart(pathsL);

	QStringList pathsR;
	for (const auto& item : r)
		pathsR.push_back(item.fullAbsolutePath());

	const auto longestCommonPrefixR = SetOperations::longestCommonStart(pathsR);

	for (auto& path : pathsL)
		path = path.mid(longestCommonPrefixL.length());

	for (auto& path : pathsR)
		path = path.mid(longestCommonPrefixR.length());

	const auto diff = SetOperations::calculateDiff(pathsL, pathsR);
	qDebug() << "Items from L not in R:";
	for (const auto& item : diff.elements_from_a_not_in_b)
		qDebug() << item;

	qDebug() << "Items from R not in L:";
	for (const auto& item : diff.elements_from_b_not_in_a)
		qDebug() << item;
}

void TestOperationPerformer::testCopy()
{
	// TODO: remove hard-coded paths
	const QString srcDirPath = QApplication::applicationDirPath() + "/../../file-commander-core/core-tests/operationperformer/test_folder/";
	const QString destDirPath = QApplication::applicationDirPath() + "/copy-move-test-folder/";

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
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath, destTree);

	if (sourceTree != destTree)
	{
		printFolderComparison(sourceTree, destTree);
		const auto diff = SetOperations::calculateDiff(sourceTree, destTree);
		QVERIFY(false);
	}
	else
		QVERIFY(true);
}

DISABLE_COMPILER_WARNINGS

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"

RESTORE_COMPILER_WARNINGS
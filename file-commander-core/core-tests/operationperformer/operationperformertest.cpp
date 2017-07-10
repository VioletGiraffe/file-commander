#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"

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
	const size_t n = std::min(l.size(), r.size());
	for (size_t i = 0; i < n; ++i)
	{
		qDebug() << l[i].fullAbsolutePath();
		qDebug() << r[i].fullAbsolutePath() << "\n";
	}

	if (l.size() == r.size())
		return;

	const auto& largerList = l.size() > r.size() ? l : r;
	if (l.size() > r.size())
		qDebug() << "Left list is larger. Extra items items from L:";
	else
		qDebug() << "Right list is larger. Extra items items from R:";

	for (size_t i = n, size = std::max(l.size(), r.size()); i < size; ++i)
		qDebug() << largerList[i].fullAbsolutePath();
}

void TestOperationPerformer::testCopy()
{
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
		QVERIFY(false);
	}
	else
		QVERIFY(true);
}

DISABLE_COMPILER_WARNINGS

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"

RESTORE_COMPILER_WARNINGS
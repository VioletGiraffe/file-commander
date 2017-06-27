#include "fileoperations/coperationperformer.h"

#include <QStringBuilder>
#include <QtTest>

class TestOperationPerformer : public QObject
{
	Q_OBJECT

private slots:
	void testCopy();
};

void TestOperationPerformer::testCopy()
{
	std::vector<CFileSystemObject> sources;
	sources.emplace_back("E:\\Development\\Projects\\Personal\\file-commander\\file-commander-core\\tests\\operationperformer\\test_folder\\");
	const QString dest = "E:/Development/Projects/Personal/file-commander/bin/copy-move-test-folder/";
#ifdef _WIN32
	std::system((QString("rmdir /S /Q ") % '\"' % QString(dest).replace('/', '\\') % '\"').toUtf8().data());
#endif
	COperationPerformer p(operationCopy, sources, dest);
	p.start();
	while (!p.done());

	QVERIFY(true);
}

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"
#include <QtTest>

class TestOperationPerformer : public QObject
{
	Q_OBJECT

private slots:
	void init();
	void testCopy();

	void cleanup();
};

void TestOperationPerformer::init()
{
}

void TestOperationPerformer::testCopy()
{
	QVERIFY(true);
}

void TestOperationPerformer::cleanup()
{
}

QTEST_MAIN(TestOperationPerformer)
#include "operationperformertest.moc"
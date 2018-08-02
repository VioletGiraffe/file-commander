#include "filecomparator/cfilecomparator.h"
#include "crandomdatagenerator.h"

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

#include "catch2_utils.hpp"

#include <QDir>
#include <QTemporaryDir>

TEST_CASE("CFileComparator identical files tests", "[CFileComparator]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_XXXXXX");
	CRandomDataGenerator gen;
	QFile fileA(sourceDirectory.filePath("A")), fileB(sourceDirectory.filePath("B"));
	for (int i = 0; i < 1000; ++i)
	{
		const int length = gen.randomInt(1000, 10 * 1024 * 1024);
		const auto data = gen.randomString(length).toLatin1();
		if (!fileA.open(QFile::ReadWrite) || !fileB.open(QFile::ReadWrite))
		{
			FAIL();
			return;
		}

		if (fileA.write(data) != data.size() || fileB.write(data) != data.size())
		{
			FAIL();
			return;
		}

		fileA.close();
		fileB.close();

		if (!fileA.open(QFile::ReadOnly) || !fileB.open(QFile::ReadOnly))
		{
			FAIL();
			return;
		}

		CFileComparator comparator;
		comparator.compareFiles(fileA, fileB, [](int) {}, [](CFileComparator::ComparisonResult result) {
			CHECK(result == CFileComparator::Equal);
		});

		fileA.close();
		fileB.close();
	}
}

TEST_CASE("CFileComparator differing files tests", "[CFileComparator]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_XXXXXX");
	CRandomDataGenerator gen;
	QFile fileA(sourceDirectory.filePath("A")), fileB(sourceDirectory.filePath("B"));
	for (int i = 0; i < 1000; ++i)
	{
		const int length = gen.randomInt(1000, 10 * 1024 * 1024);
		if (!fileA.open(QFile::ReadWrite) || !fileB.open(QFile::ReadWrite))
		{
			FAIL();
			return;
		}

		if (fileA.write(gen.randomString(length).toLatin1()) != length || fileB.write(gen.randomString(length).toLatin1()) != length)
		{
			FAIL();
			return;
		}

		fileA.close();
		fileB.close();

		if (!fileA.open(QFile::ReadOnly) || !fileB.open(QFile::ReadOnly))
		{
			FAIL();
			return;
		}

		CFileComparator comparator;
		comparator.compareFiles(fileA, fileB, [](int) {}, [](CFileComparator::ComparisonResult result) {
			CHECK(result == CFileComparator::NotEqual);
		});

		fileA.close();
		fileB.close();
	}
}

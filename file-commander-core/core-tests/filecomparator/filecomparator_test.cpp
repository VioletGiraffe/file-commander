#include "filecomparator/filecontentcomparison.h"
#include "crandomdatagenerator.h"


// Submodule includes
#include "3rdparty/catch2/catch_runner.h"
#include "compiler/compiler_warnings_control.h"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "timing/ctimeelapsed.h"


DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/catch_qt.hpp" // qtutils

#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <iostream>

static uint32_t g_randomSeed = 0; // std::random seed

TEST_CASE("CFileComparator identical files tests", "[CFileComparator]")
{
	QTemporaryDir sourceDirectory;
	if (!sourceDirectory.isValid())
	{
		FAIL();
		return;
	}

	CRandomDataGenerator gen;
	gen.setSeed(g_randomSeed);
	QFile fileA(sourceDirectory.filePath(QSL("A"))), fileB(sourceDirectory.filePath(QSL("B")));
	CTimeElapsed timer(true);
	timer.pause();
	for (int i = 0; i < 500; ++i)
	{
		const int length = gen.randomNumber<int>(10, 3 * 1024 * 1024);
		const auto data = gen.randomString(length).toLatin1();
		if (!fileA.open(QFile::WriteOnly) || !fileB.open(QFile::WriteOnly))
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

		timer.resume();
		CHECK(compareFileContents(fileA.fileName(), fileB.fileName()) == ContentComparisonResult::Equal);
		timer.pause();
	}

	std::cout << "Total time taken to process 1000 randomly sized files: " << (float)timer.elapsed() / 1000.0f;
}

TEST_CASE("CFileComparator differing files tests", "[CFileComparator]")
{
	QTemporaryDir sourceDirectory;
	CRandomDataGenerator gen;
	gen.setSeed(g_randomSeed);
	QFile fileA{sourceDirectory.filePath(QSL("A"))}, fileB{sourceDirectory.filePath(QSL("B"))};

	SECTION("Completely random data")
	{
		for (int i = 0; i < 500; ++i)
		{
			const int length = gen.randomNumber<int>(10, 3 * 1024 * 1024);
			if (!fileA.open(QFile::ReadWrite) || !fileB.open(QFile::ReadWrite))
			{
				FAIL();
				return;
			}

			const auto dataA = gen.randomString(length).toLatin1();
			const auto dataB = gen.randomString(length).toLatin1();
			if (fileA.write(dataA) != length || fileB.write(dataB) != length)
			{
				FAIL();
				return;
			}

			fileA.close();
			fileB.close();

			CHECK(compareFileContents(fileA.fileName(), fileB.fileName()) == ContentComparisonResult::Different);
		}
	}

	SECTION("Data differing in just one byte")
	{
		for (int i = 0; i < 500; ++i)
		{
			const int length = gen.randomNumber<int>(10, 3 * 1024 * 1024);
			if (!fileA.open(QFile::ReadWrite) || !fileB.open(QFile::ReadWrite))
			{
				FAIL();
				return;
			}

			const QByteArray dataA = gen.randomString(length).toLatin1();
			QByteArray dataB = dataA;
			dataB[dataB.size() - 1] = static_cast<char>(~(int)dataB[dataB.size() - 1]);
			if (fileA.write(dataA) != length || fileB.write(dataB) != length)
			{
				FAIL();
				return;
			}

			fileA.close();
			fileB.close();

			CHECK(compareFileContents(fileA.fileName(), fileB.fileName()) == ContentComparisonResult::Different);
		}
	}
}

int main(int argc, char* argv[])
{
	return runCatchSession(argc, argv, { .randomSeed = &g_randomSeed });
}

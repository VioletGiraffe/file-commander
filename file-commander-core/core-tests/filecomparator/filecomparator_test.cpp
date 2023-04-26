#include "filecomparator/cfilecomparator.h"
#include "crandomdatagenerator.h"

#include "qtcore_helpers/qstring_helpers.hpp"

#include "system/ctimeelapsed.h"
#include "compiler/compiler_warnings_control.h"

#define CATCH_CONFIG_RUNNER
#include "3rdparty/catch2/catch.hpp"

#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
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

		if (!fileA.open(QFile::ReadOnly) || !fileB.open(QFile::ReadOnly))
		{
			FAIL();
			return;
		}

		CFileComparator comparator;
		timer.resume();
		comparator.compareFiles(fileA, fileB, [](int) {}, [](CFileComparator::ComparisonResult result) {
			CHECK(result == CFileComparator::Equal);
		});
		timer.pause();

		fileA.close();
		fileB.close();
	}

	std::cout << "Total time taken to process 1000 randomly sized files: " << (float)timer.elapsed() / 1000.0f;
}

TEST_CASE("CFileComparator differing files tests", "[CFileComparator]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() % "/" % CURRENT_TEST_NAME.c_str() % "_XXXXXX");
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
}

int main(int argc, char* argv[])
{
	Catch::Session session; // There must be exactly one instance

							// Build a new parser on top of Catch's
	using namespace Catch::clara;
	auto cli
		= session.cli() // Get Catch's composite command line parser
		| Opt(g_randomSeed, "std::random seed") // bind variable to a new option, with a hint string
		["--std-seed"]        // the option names it will respond to
	("std::random seed"); // description string for the help output

						  // Now pass the new composite back to Catch so it uses that
	session.cli(cli);

	// Let Catch (using Clara) parse the command line
	const int returnCode = session.applyCommandLine(argc, argv);
	if (returnCode != 0) // Indicates a command line error
		return returnCode;

	return session.run();
}

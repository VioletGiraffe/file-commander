#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "ctestfoldergenerator.h"
#include "system/ctimeelapsed.h"
#include "filecomparator/cfilecomparator.h"

// test_utils
#include "foldercomparator.h"
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

#include "lang/type_traits_fast.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDir>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <iostream>
#include <random>
#include <string>

#define CATCH_CONFIG_RUNNER
#include "3rdparty/catch2/catch.hpp"
//#include "3rdparty/catch2/catch_template_test_macros.hpp"

static uint32_t g_randomSeed = []{
	std::random_device rd;
	return std::uniform_int_distribution<uint32_t>{0, uint32_max}(rd);
}();

static constexpr QFileDevice::FileTime supportedFileTimeTypes[] {
	QFileDevice::FileAccessTime,
#ifndef __linux__
	QFileDevice::FileBirthTime,
#endif
#if !defined __linux__ && !defined _WIN32
	QFileDevice::FileMetadataChangeTime,
#endif
	QFileDevice::FileModificationTime,
};

static bool timesAlmostMatch(const QDateTime& t1, const QDateTime& t2, const QFileDevice::FileTime type)
{
	const qint64 diff = ::abs(t1.toMSecsSinceEpoch() - t2.toMSecsSinceEpoch());

	qint64 allowedTimeDiffMs = 0;
	switch (type)
	{
	// These margins are generally too long, but necessary for CI systems (sloooow cloud virtualized hardware)
	case QFileDevice::FileAccessTime:
		allowedTimeDiffMs = 3000;
		break;
	case QFileDevice::FileBirthTime:
		allowedTimeDiffMs = 100;
		break;
	case QFileDevice::FileMetadataChangeTime:
		allowedTimeDiffMs = 2500;
		break;
	case QFileDevice::FileModificationTime:
		allowedTimeDiffMs = 100;
		break;
	default:
		assert(!"Unknown QFileDevice::FileTime");
		return false;
	}

#ifdef __APPLE__
	qint64 multiplier = 50;
#else
	qint64 multiplier = 1;
#endif

#ifdef _DEBUG
	multiplier *= 2;
#endif

	if (diff <= allowedTimeDiffMs * multiplier)
		return true;
	else
	{
		std::cerr << "Time mismatch for type " << type << ": diff = " << diff << ", allowed = " << allowedTimeDiffMs * multiplier << '\n';
		return false;
	}
}

struct ProgressObserver final : public CFileOperationObserver {
	inline void onProgressChanged(float totalPercentage, size_t /*numFilesProcessed*/, size_t /*totalNumFiles*/, float filePercentage, uint64_t /*speed*/ /* B/s*/, uint32_t /*secondsRemaining*/) override {
		CHECK(totalPercentage <= 100.1f);
		CHECK(filePercentage <= 100.1f);
	}
	inline void onProcessHalted(HaltReason /*reason*/, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override { // User decision required (file exists, file is read-only etc.)
		FAIL("onProcessHalted called");
	}
	inline void onProcessFinished(const QString& /*message*/ = QString()) override {} // Done or canceled
	inline void onCurrentFileChanged(const QString& /*file*/) override {} // Starting to process a new file
};

static void copyTest(const size_t maxFileSize)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	REQUIRE(QFileInfo::exists(targetDirectory.path()));
	REQUIRE(CFileSystemObject(targetDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, maxFileSize));

	COperationPerformer p(operationCopy, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	CTimeElapsed timer(true);

	ProgressObserver progressObserver;
	p.setObserver(&progressObserver);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(sourceDirectory.path(), sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(targetDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName(), destTree);

	CFileComparator comparator;
	REQUIRE(sourceTree.size() == destTree.size());

	for (size_t i = 0, n = sourceTree.size(); i < n; ++i)
	{
		const auto& sourceItem = sourceTree[i];
		const auto& destItem = destTree[i];
		if (!sourceItem.isFile())
			continue;

		QFile fileA(sourceItem.fullAbsolutePath());
		REQUIRE(fileA.open(QFile::ReadOnly));

		QFile fileB(destItem.fullAbsolutePath());
		REQUIRE(fileB.open(QFile::ReadOnly));

		comparator.compareFiles(fileA, fileB, [](int) {}, [&fileA, &fileB](const CFileComparator::ComparisonResult result) {
			if (result != CFileComparator::Equal) [[unlikely]]
			{
				std::string msg;
				if (fileA.size() != fileB.size())
					msg = "Files are not equal (sizes differ: " + std::to_string(fileA.size()) + " and " + std::to_string(fileB.size()) + "): ";
				else
					msg = "Files are not equal: ";

				msg += fileA.fileName().toStdString() + " and " + fileB.fileName().toStdString();
				FAIL_CHECK(msg);
			}
			else
				CHECK(true);
			});

		for (const auto fileTimeType : supportedFileTimeTypes)
		{
			if (!timesAlmostMatch(fileA.fileTime(fileTimeType), fileB.fileTime(fileTimeType), fileTimeType))
				FAIL_CHECK();
		}
	}

	REQUIRE(compareFolderContents(sourceTree, destTree));
}

static void moveTest(const size_t maxFileSize)
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	QTemporaryDir targetDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_TARGET_XXXXXX");
	REQUIRE(sourceDirectory.isValid());
	REQUIRE(targetDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();
	TRACE_LOG << "Target: " << targetDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	REQUIRE(QFileInfo::exists(targetDirectory.path()));
	REQUIRE(CFileSystemObject(targetDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, maxFileSize));

	std::vector<CFileSystemObject> sourceTree;
	CFolderEnumeratorRecursive::enumerateFolder(sourceDirectory.path(), sourceTree);

	COperationPerformer p(operationMove, CFileSystemObject(sourceDirectory.path()), targetDirectory.path());
	ProgressObserver progressObserver;
	p.setObserver(&progressObserver);

	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	REQUIRE(!CFileSystemObject(sourceDirectory.path()).exists());

	std::vector<CFileSystemObject> destTree;
	CFolderEnumeratorRecursive::enumerateFolder(targetDirectory.path() % '/' % QFileInfo(sourceDirectory.path()).completeBaseName(), destTree);

	REQUIRE(compareFolderContents(sourceTree, destTree));
}

TEST_CASE((std::string("Copy test - empty files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(0);
}

TEST_CASE((std::string("Copy test - 5K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(5000);
}

TEST_CASE((std::string("Copy test - 20K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	copyTest(20'000);
}

TEST_CASE((std::string("Move test - empty files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	moveTest(0);
}

TEST_CASE((std::string("Move test - 5K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	moveTest(5000);
}

TEST_CASE((std::string("Move test - 20K files #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
{
	moveTest(20'000);
}

TEST_CASE((std::string("Delete test #") + std::to_string(rand())).c_str(), "[operationperformer-delete]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	REQUIRE(sourceDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setFileChunkSize(OPERATION_PERFORMER_CHUNK_SIZE);
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200, 20000));
	REQUIRE(!QDir(sourceDirectory.path()).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).empty());

	COperationPerformer p(operationDelete, CFileSystemObject(sourceDirectory.path()));
	ProgressObserver progressObserver;
	p.setObserver(&progressObserver);

	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
		progressObserver.processEvents();
#ifndef _DEBUG
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2ull * 60ull)
		{
			FAIL("File operation timeout reached.");
			return;
		}
#endif
	}

	REQUIRE(!CFileSystemObject(sourceDirectory.path()).exists());
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

	srand(g_randomSeed);

	{
		CRandomDataGenerator _randomGenerator;
		_randomGenerator.setSeed(g_randomSeed);
		Logger() << "RNG consustency check: seed = " << g_randomSeed <<", first RN = " << _randomGenerator.randomNumber<uint32_t>(0u, uint32_max);
	}

	return session.run();
}

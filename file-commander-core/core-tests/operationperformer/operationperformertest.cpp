#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "ctestfoldergenerator.h"
#include "system/ctimeelapsed.h"
#include "filecomparator/cfilecomparator.h"

// test_utils
#include "foldercomparator.h"
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QDir>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <iostream>
#include <string>

#define CATCH_CONFIG_RUNNER
#include "3rdparty/catch2/catch.hpp"

static uint32_t g_randomSeed = 1633456005;

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
	const auto diff = ::abs(t1.toMSecsSinceEpoch() - t2.toMSecsSinceEpoch());

	int allowedTimeDiffMs = 0;
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
	static constexpr int multiplier = 50;
#else
	static constexpr int multiplier = 1;
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
		CHECK(totalPercentage <= 100.0f);
		CHECK(filePercentage <= 100.0f);
	}
	inline void onProcessHalted(HaltReason /*reason*/, const CFileSystemObject& /*source*/, const CFileSystemObject& /*dest*/, const QString& /*errorMessage*/) override { // User decision required (file exists, file is read-only etc.)
		FAIL("onProcessHalted called");
	}
	inline void onProcessFinished(const QString& /*message*/ = QString()) override {} // Done or canceled
	inline void onCurrentFileChanged(const QString& /*file*/) override {} // Starting to process a new file
};

TEST_CASE((std::string("Copy test #") + std::to_string(rand())).c_str(), "[operationperformer-copy]")
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
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200));

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
		if (timer.elapsed<std::chrono::seconds>() > 2 * 60)
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

		comparator.compareFiles(fileA, fileB, [](int) {}, [](const CFileComparator::ComparisonResult result) {
			CHECK(result == CFileComparator::Equal);
		});

		for (const auto fileTimeType: supportedFileTimeTypes)
		{
			if (!timesAlmostMatch(fileA.fileTime(fileTimeType), fileB.fileTime(fileTimeType), fileTimeType))
				FAIL_CHECK();
		}
	}

	REQUIRE(compareFolderContents(sourceTree, destTree));
}

TEST_CASE((std::string("Move test #") + std::to_string(rand())).c_str(), "[operationperformer-move]")
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
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200));

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
		if (timer.elapsed<std::chrono::seconds>() > 2 * 60)
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

TEST_CASE((std::string("Delete test #") + std::to_string(rand())).c_str(), "[operationperformer-delete]")
{
	QTemporaryDir sourceDirectory(QDir::tempPath() + "/" + CURRENT_TEST_NAME.c_str() + "_SOURCE_XXXXXX");
	REQUIRE(sourceDirectory.isValid());

	TRACE_LOG << "Source: " << sourceDirectory.path();

	REQUIRE(QFileInfo::exists(sourceDirectory.path()));
	REQUIRE(CFileSystemObject(sourceDirectory.path()).isEmptyDir());

	CTestFolderGenerator generator;
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(sourceDirectory.path(), 1000, 200));
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
		if (timer.elapsed<std::chrono::seconds>() > 2 * 60)
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

	return session.run();
}

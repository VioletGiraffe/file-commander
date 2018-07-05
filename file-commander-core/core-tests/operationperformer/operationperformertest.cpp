#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "ctestfoldergenerator.h"
#include "container/set_operations.hpp"
#include "system/processfilepath.hpp"
#include "system/ctimeelapsed.h"

// test_utils
#include "foldercomparator.h"
#include "qt_helpers.hpp"
#include "catch2_utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <iostream>

#define CATCH_CONFIG_RUNNER
#include "../catch2/catch.hpp"

static uint32_t g_randomSeed = 0;

inline QString srcTestDirPath()
{
	return QFileInfo(qStringFromWstring(processFilePath()) + "/../test_directory_source/").absoluteFilePath();
}

inline QString dstTestDirPath()
{
	return QFileInfo(qStringFromWstring(processFilePath()) + "/../test_directory_target/").absoluteFilePath();
}

TEST_CASE("fileSystemObjectTest", "[operationperformer]")
{
	CFileSystemObject o(srcTestDirPath());
	CHECK(o.fullAbsolutePath() != (o.parentDirPath() + '/'));
}

inline bool deleteDirectoryWithContents(const QString& dirPath)
{
	if (!QFileInfo::exists(dirPath))
		return true;

#ifdef _WIN32
	return std::system((QString("rmdir /S /Q ") % '\"' % QString(dirPath).replace('/', '\\') % '\"').toUtf8().data()) == 0;
#else
	return std::system((QString("rm -rf ") % '\"' % QString(dirPath) % '\"').toUtf8().data()) == 0;
#endif
}

TEST_CASE("Copy test", "[operationperformer]")
{
	const QString srcDirPath = srcTestDirPath();
	const QString destDirPath = dstTestDirPath();

	TRACE_LOG << "Source: " << srcDirPath;
	TRACE_LOG << "Target: " << destDirPath;

	if (deleteDirectoryWithContents(srcDirPath) == false)
	{
		FAIL("Clearing the source folder: FAILED!");
		return;
	}
	else
		TRACE_LOG << "Clearing the source folder: SUCCESS";

	if (deleteDirectoryWithContents(destDirPath) == false)
	{
		FAIL("Clearing the target folder: FAILED!");
		return;
	}
	else
		TRACE_LOG << "Clearing the target folder: SUCCESS";

	REQUIRE(!QFileInfo::exists(srcDirPath));
	REQUIRE(!QFileInfo::exists(destDirPath));
	REQUIRE(QDir(srcDirPath).mkpath(".") == true);

	CTestFolderGenerator generator;
	generator.setSeed(g_randomSeed);
	TRACE_LOG << "std::random seed: " << g_randomSeed;
	REQUIRE(generator.generateRandomTree(srcDirPath, 1000, 200));

	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(srcDirPath)}, destDirPath);
	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
		// Reasonable timeout
		if (timer.elapsed<std::chrono::seconds>() > 2 * 60)
		{
			FAIL("File operation timeout reached.");
			return;
		}
	}

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(srcDirPath, sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath + CFileSystemObject(srcTestDirPath()).fullName(), destTree);

	REQUIRE(compareFolderContents(sourceTree, destTree));
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
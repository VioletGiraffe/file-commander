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
#include <QDir>
#include <QStringBuilder>
#include <QTemporaryDir>
RESTORE_COMPILER_WARNINGS

#include <iostream>
#include <string>

#define CATCH_CONFIG_RUNNER
#include "../catch2/catch.hpp"

static uint32_t g_randomSeed = 0;

TEST_CASE((std::string("Copy test #") + std::to_string((srand(time(nullptr)), rand()))).c_str(), "[operationperformer]")
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

	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(sourceDirectory.path())}, targetDirectory.path());
	CTimeElapsed timer(true);
	p.start();
	while (!p.done())
	{
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
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

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

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

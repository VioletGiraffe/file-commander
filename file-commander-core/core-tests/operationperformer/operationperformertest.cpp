#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "ctestfoldergenerator.h"
#include "container/set_operations.hpp"
#include "system/processfilepath.hpp"

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
		TRACE_LOG << "Clearing the source folder: FAILED!";
		FAIL();
		return;
	}
	else
		TRACE_LOG << "Clearing the source folder: SUCCESS";

	if (deleteDirectoryWithContents(destDirPath) == false)
	{
		TRACE_LOG << "Clearing the target folder: FAILED!";
		FAIL();
		return;
	}
	else
		TRACE_LOG << "Clearing the target folder: SUCCESS";

	CTestFolderGenerator generator;
	REQUIRE(generator.generateRandomTree(destDirPath, 1000, 200));
	SUCCEED();
	return;

	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(srcDirPath)}, destDirPath);
	p.start();
	while (!p.done());

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(srcDirPath, sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath + CFileSystemObject(srcTestDirPath()).fullName(), destTree);

	CHECK(compareFolderContents(sourceTree, destTree));
}

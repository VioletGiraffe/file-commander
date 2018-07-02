#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "container/set_operations.hpp"
#include "system/processfilepath.hpp"

// test_utils
#include "foldercomparator.h"
#include "qt_helpers.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <iostream>

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

inline QString srcTestDirPath()
{
	const auto selfExecutablePath = processFilePath();
	const QString absolutePath = QFileInfo(QString::fromWCharArray(selfExecutablePath.data(), (int)selfExecutablePath.size())).absoluteFilePath();
	return absolutePath + "/../../file-commander-core/core-tests/operationperformer/test_folder/";
}

inline QString dstTestDirPath()
{
	const auto selfExecutablePath = processFilePath();
	const QString absolutePath = QFileInfo(QString::fromWCharArray(selfExecutablePath.data(), (int)selfExecutablePath.size())).absoluteFilePath();
	return absolutePath + "/copy-move-test-folder/";
}

TEST_CASE("fileSystemObjectTest", "[operationperformer]")
{
	CFileSystemObject o(srcTestDirPath());
	CHECK(o.fullAbsolutePath() != (o.parentDirPath() + '/'));
}

TEST_CASE("Copy test", "[operationperformer]")
{
	// TODO: remove hard-coded paths
	const QString srcDirPath = srcTestDirPath();
	const QString destDirPath = dstTestDirPath();

	std::cout << "Source:" << srcDirPath;
	std::cout << "Dest:" << destDirPath;

	// TODO: extract this into init() / cleanup()
#ifdef _WIN32
	std::system((QString("rmdir /S /Q ") % '\"' % QString(destDirPath).replace('/', '\\') % '\"').toUtf8().data());
#else
	std::system((QString("rm -rf ") % '\"' % QString(destDirPath) % '\"').toUtf8().data());
#endif
	COperationPerformer p(operationCopy, std::vector<CFileSystemObject> {CFileSystemObject(srcDirPath)}, destDirPath);
	p.start();
	while (!p.done());

	std::vector<CFileSystemObject> sourceTree, destTree;
	CFolderEnumeratorRecursive::enumerateFolder(srcDirPath, sourceTree);
	CFolderEnumeratorRecursive::enumerateFolder(destDirPath + CFileSystemObject(srcTestDirPath()).fullName(), destTree);

	CHECK(compareFolderContents(sourceTree, destTree));
}

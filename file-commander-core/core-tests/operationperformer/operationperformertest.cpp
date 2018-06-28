#include "fileoperations/coperationperformer.h"
#include "cfolderenumeratorrecursive.h"
#include "container/set_operations.hpp"
#include "../test-utils/src/qt_helpers.hpp"
#include "system/processfilepath.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <iostream>

#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"

inline bool compareFolderContents(const std::vector<CFileSystemObject>& source, const std::vector<CFileSystemObject>& dest)
{
	QStringList pathsSource;
	for (const auto& item : source)
		pathsSource.push_back(item.fullAbsolutePath());

	// https://stackoverflow.com/questions/51009172/erroneous-ambiguous-base-class-error-in-template-context
	const auto longestCommonPrefixL = SetOperations::longestCommonStart((const QList<QString>&)pathsSource);

	QStringList pathsDest;
	for (const auto& item : dest)
		pathsDest.push_back(item.fullAbsolutePath());

	const auto longestCommonPrefixR = SetOperations::longestCommonStart((const QList<QString>&)pathsDest);

	for (auto& path : pathsSource)
		path = path.mid(longestCommonPrefixL.length());

	for (auto& path : pathsDest)
		path = path.mid(longestCommonPrefixR.length());

	bool differenceDetected = false;

	const auto diff = SetOperations::calculateDiff(pathsSource, pathsDest);
	if (!diff.elements_from_a_not_in_b.empty())
	{
		differenceDetected = true;
		std::cout << "Items from source not in dest:";
		for (const auto& item : diff.elements_from_a_not_in_b)
			std::cout << item;
	}

	if (!diff.elements_from_b_not_in_a.empty())
	{
		differenceDetected = true;
		std::cout << "Items from dest not in source:";
		for (const auto& item : diff.elements_from_b_not_in_a)
			std::cout << item;
	}

	return !differenceDetected;
}

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

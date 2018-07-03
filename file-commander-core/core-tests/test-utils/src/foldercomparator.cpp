#include "foldercomparator.h"
#include "qt_helpers.hpp"

#include "compiler/compiler_warnings_control.h"
#include "container/set_operations.hpp"
#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <iostream>

// TODO: also compare all the files by contents.
bool compareFolderContents(const std::vector<CFileSystemObject>& source, const std::vector<CFileSystemObject>& dest)
{
	if (source.size() != dest.size())
		return false;

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

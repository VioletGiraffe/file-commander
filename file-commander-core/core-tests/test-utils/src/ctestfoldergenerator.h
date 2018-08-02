#pragma once
#include "crandomdatagenerator.h"

class QString;

#include <vector>

class CTestFolderGenerator
{
public:
	void setSeed(uint32_t seed);
	bool generateRandomTree(const QString& parentDir, size_t numFiles, size_t numFolders);

private:
	QString randomFileName(const size_t length);
	QString randomDirName(const size_t length);

	bool generateRandomFiles(const QString& parentDir, const size_t numFiles);
	std::vector<QString> generateRandomFolders(const QString& parentDir, const size_t numFolders);

private:
	CRandomDataGenerator _randomGenerator;
};

#pragma once
#include "crandomdatagenerator.h"

class QString;

#include <vector>

class CTestFolderGenerator
{
public:
	void setSeed(uint32_t seed);
	void setFileChunkSize(size_t chunkSize);

	bool generateRandomTree(const QString& parentDir, size_t numFiles, size_t numFolders);

private:
	QString randomFileName(size_t length);
	QString randomDirName(size_t length);

	bool generateRandomFiles(const QString& parentDir, size_t numFiles);
	std::vector<QString> generateRandomFolders(const QString& parentDir, size_t numFolders);

private:
	bool createRandomFile(const QString& parentDir, size_t fileSize);

private:
	CRandomDataGenerator _randomGenerator;
	size_t _fileChunkSize = 0;
};

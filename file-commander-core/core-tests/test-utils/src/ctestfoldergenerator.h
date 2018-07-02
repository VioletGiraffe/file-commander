#pragma once

class QString;

#include <random>
#include <vector>

class CTestFolderGenerator
{
public:
	void setSeed(const std::mt19937::result_type seed);
	bool generateRandomTree(const QString& parentDir, const uint32_t numFiles, const uint32_t numFolders);

private:
	QString randomString(const size_t length);
	bool generateRandomFiles(const QString& parentDir, const uint32_t numFiles);
	std::vector<QString> generateRandomFolders(const QString& parentDir, const uint32_t numFolders);

private:
	std::mt19937 _rng;
};

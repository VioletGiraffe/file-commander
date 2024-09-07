#include "ctestfoldergenerator.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <limits>
#include <utility>

void CTestFolderGenerator::setSeed(const uint32_t seed)
{
	_randomGenerator.setSeed(seed);
}

void CTestFolderGenerator::setFileChunkSize(size_t chunkSize)
{
	_fileChunkSize = chunkSize;
}

bool CTestFolderGenerator::generateRandomTree(const QString& parentDir, size_t numFiles, size_t numFolders, size_t maxFilesSize)
{
	assert(QDir(parentDir).exists());

	if (numFolders == 0)
	{
		if (numFiles == 0)
			return true;

		// All the remaining files must go into this last unpopulated directory
		assert_and_return_r(generateRandomFiles(parentDir, numFiles, maxFilesSize), false);
		return true; // All done.
	}

	std::vector<QString> folders;
	assert_and_return_r(generateFolderTree(parentDir, (int)numFolders, folders), false);

	for (size_t i = 0; i < numFiles; ++i)
	{
		// Create a file in a random folder
		const size_t index = _randomGenerator.randomNumber<size_t>(0u, folders.size() - 1);
		assert_and_return_r(generateRandomFiles(folders[index], 1, maxFilesSize), false);
	}

	return true;
}

QString CTestFolderGenerator::randomFileName(const size_t length)
{
	assert_and_return_r(length > 3, QString());
	QString extension;
	do
	{
		extension = _randomGenerator.randomString(3);
	}
	while (extension == QLatin1String("lnk"));

	return _randomGenerator.randomString(length).append('.').append(extension);
}

QString CTestFolderGenerator::randomDirName(const size_t length)
{
	return _randomGenerator.randomString(length);
}

bool CTestFolderGenerator::generateRandomFiles(const QString& parentDir, const size_t numFiles, size_t maxFilesSize)
{
	const size_t numZeroFiles = [numFiles]() -> size_t {
		if (numFiles > 20)
			return std::min(size_t{ 2 }, numFiles / 100u);
		else if (numFiles <= 1)
			return 0;
		else
			return 1;
	}();

	const auto numSpecialFiles = [nFiles{numFiles - numZeroFiles}, this, maxFilesSize]() -> size_t {
		size_t n = 0;
		if (_fileChunkSize == 0 || nFiles < 3)
			n = 0;
		else if (nFiles > 30)
			n = 10;
		else
			n = nFiles / 3;

		if (maxFilesSize < n * _fileChunkSize)
			n = maxFilesSize / _fileChunkSize;

		return n;
	}();

	for (uint32_t i = 0; i < numFiles - numZeroFiles - numSpecialFiles; ++i)
	{
		const size_t fileSize = maxFilesSize > 1 ? _randomGenerator.randomNumber<size_t>(1, maxFilesSize) : maxFilesSize;
		createRandomFile(parentDir, fileSize);
	}

	// Add a bunch of 0-sized files, as well as files with size being a multiple of chunk size
	for (size_t i = 0; i < numZeroFiles; ++i)
	{
		createRandomFile(parentDir, 0);
	}

	for (size_t i = 0; i < numSpecialFiles; ++i)
	{
		createRandomFile(parentDir, (i + 1) * _fileChunkSize);
	}

	return true;
}

std::vector<QString> CTestFolderGenerator::generateRandomFolders(const QString& parentDir, const size_t numFolders)
{
	std::vector<QString> newFolderNames;
	newFolderNames.reserve(numFolders);

	QDir parentFolder(parentDir);
	for (uint32_t i = 0; i < numFolders; ++i)
	{
		const auto newFolderName = randomDirName(12);
		assert_and_return_r(parentFolder.mkdir(newFolderName), std::vector<QString>());
		newFolderNames.emplace_back(newFolderName);
	}

	return newFolderNames;
}

std::optional<int> CTestFolderGenerator::generateFolderTree(const QString& parentDir, const int numFolders, std::vector<QString>& folders)
{
	if (numFolders <= 0)
		return 0;

	static constexpr auto sqrti = [](int i) -> int {
		return (int)::sqrtf((float)i);
	};

	const int n = _randomGenerator.randomNumber<int>(1, std::min(numFolders, sqrti(numFolders) * 2));
	const auto subfolders = generateRandomFolders(parentDir, n);

	int nRemaining = numFolders - (int)subfolders.size();

	while (nRemaining > 0) // Keep running until all done
	{
		for (const QString& subfolder : subfolders)
		{
			const QString fullPath = folders.emplace_back(parentDir + '/' + subfolder);

			const int n = _randomGenerator.randomNumber<int>(1, nRemaining * 4 / (int)subfolders.size());
			const auto result = generateFolderTree(fullPath, n, folders);
			assert_and_return_r(result, {});
			nRemaining -= result.value();

			if (nRemaining <= 0)
				break;
		}
	}

	return numFolders;
}

bool CTestFolderGenerator::createRandomFile(const QString& parentDir, size_t fileSize)
{
	QFile file(parentDir + '/' + randomFileName(16));
	assert_and_return_r(file.open(QFile::WriteOnly), false);

	QByteArray randomData;
	randomData.reserve((int)fileSize);

	for (; fileSize >= sizeof(uint64_t); fileSize -= sizeof(uint64_t))
	{
		const uint64_t randomBytes = _randomGenerator.randomNumber<uint64_t>(0, std::numeric_limits<uint64_t>::max());
		randomData.append(reinterpret_cast<const char*>(&randomBytes), sizeof(randomBytes));
	}

	while (fileSize --> 0)
	{
		const uint8_t randomByte = static_cast<uint8_t>(_randomGenerator.randomNumber<uint16_t>(0, 255));
		randomData.append(static_cast<char>(randomByte));
	}

	assert_and_return_r(file.write(randomData) == (qint64)randomData.size(), false);
	return true;
}

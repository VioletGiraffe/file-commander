#include "ctestfoldergenerator.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <utility>

void CTestFolderGenerator::setSeed(const std::mt19937::result_type seed)
{
	_rng = std::mt19937(seed);
}

bool CTestFolderGenerator::generateRandomTree(const QString& parentDir, size_t numFiles, size_t numFolders)
{
	assert(QDir(parentDir).exists());

	if (numFolders == 0)
	{
		if (numFiles == 0)
			return true;

		// All the remaining files must go into this last unpopulated directory
		assert_and_return_r(generateRandomFiles(parentDir, numFiles), false); // Failure?
		return true; // All done.
	}

	std::vector<QString> newFolders;

	{
		std::uniform_int_distribution<size_t> numFoldersDistribution(1, numFolders);
		const auto numFoldersToCreate = numFoldersDistribution(_rng);
		newFolders = generateRandomFolders(parentDir, numFoldersToCreate);
		assert_and_return_r(!newFolders.empty(), false); // Failure?
		numFolders -= numFoldersToCreate;
	}

	if (numFiles > 0)
	{
		std::uniform_int_distribution<size_t> numFilesDistribution(1, numFiles);
		const auto numFilesToCreate = numFilesDistribution(_rng);
		assert_and_return_r(generateRandomFiles(parentDir, numFilesToCreate), false); // Failure?
		numFiles -= numFilesToCreate;
	}

	const auto nDirectoriesPerSubdirectory = numFolders / newFolders.size();
	const auto nFilesPerSubdirectory = numFiles / newFolders.size();

	for (const QString& folder : newFolders)
	{
		assert_and_return_r(generateRandomTree(parentDir + "/" + folder, nFilesPerSubdirectory, nDirectoriesPerSubdirectory), false); // Failure?
	}

	numFiles = numFiles % newFolders.size();
	numFolders = numFolders % newFolders.size();

	return generateRandomTree(parentDir, numFiles, numFolders);
}

QString CTestFolderGenerator::randomString(const size_t length)
{
	std::vector<QChar> chars;
	chars.reserve(length);

	std::uniform_int_distribution<short> distribution('a', 'z');
	for (size_t i = 0; i < length; ++i)
		chars.emplace_back(static_cast<char>(distribution(_rng)));

	return QString(chars.data(), (int)chars.size());
}

bool CTestFolderGenerator::generateRandomFiles(const QString& parentDir, const size_t numFiles)
{
	for (uint32_t i = 0; i < numFiles; ++i)
	{
		QFile file(parentDir + '/' + randomString(12));
		assert_and_return_r(file.open(QFile::WriteOnly), false);

		const QByteArray randomData = randomString(std::uniform_int_distribution<size_t>(10, 100)(_rng)).toUtf8();
		assert_and_return_r(file.write(randomData) == (qint64)randomData.size(), false);
	}

	return true;
}

std::vector<QString> CTestFolderGenerator::generateRandomFolders(const QString& parentDir, const size_t numFolders)
{
	QDir parentFolder(parentDir);
	std::vector<QString> newFolderNames;
	for (uint32_t i = 0; i < numFolders; ++i)
	{
		const auto newFolderName = randomString(12);
		assert_and_return_r(parentFolder.mkdir(newFolderName), std::vector<QString>());
		newFolderNames.emplace_back(newFolderName);
	}

	return newFolderNames;
}

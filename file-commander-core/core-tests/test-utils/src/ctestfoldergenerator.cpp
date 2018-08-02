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
#include <utility>

void CTestFolderGenerator::setSeed(const uint32_t seed)
{
	_randomGenerator.setSeed(seed);
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
		const auto numFoldersToCreate = _randomGenerator.randomInt(1, numFolders);
		newFolders = generateRandomFolders(parentDir, numFoldersToCreate);
		assert_and_return_r(!newFolders.empty(), false); // Failure?
		numFolders -= numFoldersToCreate;
	}

	if (numFiles > 0)
	{
		const auto numFilesToCreate = _randomGenerator.randomInt(1, numFiles);
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

QString CTestFolderGenerator::randomFileName(const size_t length)
{
	assert_and_return_r(length > 3, QString());
	const auto extension = _randomGenerator.randomString(3);
	if (extension == QLatin1String("lnk"))
		return randomFileName(length);

	return _randomGenerator.randomString(length - 3) % '.' % extension;
}

QString CTestFolderGenerator::randomDirName(const size_t length)
{
	return _randomGenerator.randomString(length).toUpper();
}

bool CTestFolderGenerator::generateRandomFiles(const QString& parentDir, const size_t numFiles)
{
	for (uint32_t i = 0; i < numFiles; ++i)
	{
		QFile file(parentDir + '/' + randomFileName(12));
		assert_and_return_r(file.open(QFile::WriteOnly), false);

		const QByteArray randomData = _randomGenerator.randomString(_randomGenerator.randomInt(10, 100)).toUtf8();
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
		const auto newFolderName = randomDirName(12);
		assert_and_return_r(parentFolder.mkdir(newFolderName), std::vector<QString>());
		newFolderNames.emplace_back(newFolderName);
	}

	return newFolderNames;
}

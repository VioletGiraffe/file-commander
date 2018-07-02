#include "ctestfoldergenerator.h"
#include "compiler/compiler_warnings_control.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <utility>

void CTestFolderGenerator::setSeed(const std::mt19937::result_type seed)
{
	_rng = std::move(std::mt19937(seed));
}

bool CTestFolderGenerator::generateRandomTree(const QString& parentDir, const uint32_t numFiles, const uint32_t numFolders)
{
	{
		QDir parentQDir(parentDir);
		assert_and_return_r(parentQDir.exists() || parentQDir.mkpath("."), false);
	}

	std::vector<QString> newFolders;
	if (numFolders > 0)
	{
		std::uniform_int_distribution<uint32_t> numFoldersDistribution(1, numFolders);
		const auto numFoldersToCreate = numFoldersDistribution(_rng);
		newFolders = generateRandomFolders(parentDir, numFoldersToCreate);
		if (newFolders.empty())
			return false;
	}

	if (numFiles > 0)
	{
		if (newFolders.size() > 0)
		{
			std::uniform_int_distribution<uint32_t> numFilesDistribution(1, numFiles);
			const auto numFilesToCreate = numFilesDistribution(_rng);
			if (!generateRandomFiles(parentDir, numFilesToCreate))
				return false;

			for (const QString& folder : newFolders)
				assert_and_return_r(generateRandomTree(parentDir + "/" + folder, numFiles - numFilesToCreate, numFolders - newFolders.size()), false);
		}
		else
		{
			// All the remaining files must go into this last unpopulated directory
			if (!generateRandomFiles(parentDir, numFiles))
				return false;
		}
	}

	return true;
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

bool CTestFolderGenerator::generateRandomFiles(const QString& parentDir, const uint32_t numFiles)
{
	for (uint32_t i = 0; i < numFiles; ++i)
	{
		QFile file(parentDir + '/' + randomString(12));
		assert_and_return_r(file.open(QFile::WriteOnly), false);

		const QByteArray randomData = randomString(30).toUtf8();
		assert_and_return_r(file.write(randomData) == (qint64)randomData.size(), false);
	}

	return true;
}

std::vector<QString> CTestFolderGenerator::generateRandomFolders(const QString& parentDir, const uint32_t numFolders)
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

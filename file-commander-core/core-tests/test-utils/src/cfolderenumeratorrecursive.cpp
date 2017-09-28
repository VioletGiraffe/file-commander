#include "cfolderenumeratorrecursive.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QDir>
RESTORE_COMPILER_WARNINGS

#include <iterator>

void CFolderEnumeratorRecursive::enumerateFolder(const QString& dirPath, std::vector<CFileSystemObject>& result, bool sort /*= true*/)
{
	auto list = QDir(dirPath).entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::System);

	std::vector<CFileSystemObject> currentFolderContents;
	for (const auto& qitem : list)
		currentFolderContents.emplace_back(qitem);

	for (const auto& item: currentFolderContents)
	{
		result.emplace_back(item);
		if (item.isDir())
			enumerateFolder(item.fullAbsolutePath(), result, false);
	}

	if (sort)
	{
		std::sort(result.begin(), result.end(), [](const CFileSystemObject& l, const CFileSystemObject& r) {
			return l.fullAbsolutePath() < r.fullAbsolutePath();
		});
	}
}

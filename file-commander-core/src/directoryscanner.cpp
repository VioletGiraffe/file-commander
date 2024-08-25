#include "directoryscanner.h"

#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QDir>
RESTORE_COMPILER_WARNINGS

void scanDirectory(const CFileSystemObject& root, const std::function<void(const CFileSystemObject&)>& observer, const std::atomic<bool>& abort)
{
	if (observer)
		observer(root);

	if (!root.isDir())
		return;

	const auto list = QDir{root.fullAbsolutePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);
	for (const auto& entry : list)
	{
		scanDirectory(CFileSystemObject(entry), observer, abort);

		if (abort)
			return;
	}
}

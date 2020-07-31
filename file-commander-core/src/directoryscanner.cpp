#include "directoryscanner.h"

void scanDirectory(const CFileSystemObject& root, const std::function<void(const CFileSystemObject&)>& observer, const std::atomic<bool>& abort)
{
	if (observer)
		observer(root);

	if (!root.isDir() || abort)
		return;

	const auto list = QDir{root.fullAbsolutePath()}.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot | QDir::System);
	for (const auto& entry : list)
	{
		scanDirectory(CFileSystemObject(entry), observer, abort);

		if (abort)
			return;
	}
}

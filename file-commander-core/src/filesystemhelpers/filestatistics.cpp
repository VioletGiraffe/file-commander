#include "filestatistics.h"
#include "../directoryscanner.h"
#include "../cfilesystemobject.h"

FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort)
{
	FileStatistics stats;
	for(const QString& path: paths)
	{
		const CFileSystemObject rootItem(path);
		if (rootItem.isDir())
		{
			::scanDirectory(rootItem, [&stats](const CFileSystemObject& discoveredItem) {
				if (discoveredItem.isFile())
				{
					stats.occupiedSpace += discoveredItem.size();
					++stats.files;
				}
				else if (discoveredItem.isDir())
					++stats.folders;
			}, abort);
		}
		else if (rootItem.isFile())
		{
			++stats.files;
			stats.occupiedSpace += rootItem.size();
		}
	}

	return stats;
}

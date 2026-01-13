#include "filestatistics.h"
#include "../directoryscanner.h"
#include "../cfilesystemobject.h"

FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort)
{
	static constexpr size_t MaxLargeFiles = 500;

	FileStatistics stats;
	for(const QString& path: paths)
	{
		const CFileSystemObject rootItem(path);
		if (rootItem.isDir())
		{
			::scanDirectory(rootItem, [&stats](const CFileSystemObject& discoveredItem) {
				if (discoveredItem.isFile())
				{
					const auto size = discoveredItem.size();
					stats.occupiedSpace += size;
					++stats.files;

					auto& largestFiles = stats.largestFiles;
					const auto nLargest = largestFiles.size();
					if (nLargest == 0) [[unlikely]]
						largestFiles.push_back(discoveredItem);
					else if (size > largestFiles.front().size())
					{
						largestFiles.push_back(discoveredItem);
						if (nLargest >= MaxLargeFiles)
							largestFiles.pop_front();
					}

				}
				else if (discoveredItem.isDir())
				{
					++stats.folders;
				}

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

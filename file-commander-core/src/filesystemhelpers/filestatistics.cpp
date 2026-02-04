#include "filestatistics.h"
#include "../directoryscanner.h"
#include "../cfilesystemobject.h"

#include <algorithm>

FileStatistics calculateStatsFor(const std::vector<QString>& paths, const std::atomic_bool& abort)
{
	static constexpr size_t MaxLargeFiles = 500;

	FileStatistics stats;
	stats.largestFiles.reserve(MaxLargeFiles + 1);

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

					const auto it = std::ranges::lower_bound(largestFiles, discoveredItem, [](const CFileSystemObject& l, const CFileSystemObject& r) {
						return l.size() > r.size();
					});
					largestFiles.insert(it, discoveredItem);
					
					if (nLargest >= MaxLargeFiles)
						largestFiles.pop_back();

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

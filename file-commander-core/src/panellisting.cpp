#include "panellisting.h"

#include "cfilesystemobject.h"


// Submodule includes
#include "directoryscan/thinioqstring.h"


#include <utility>

FileListHashMap listDirectoryForPanel(const QString& dirPath, const bool showHiddenFiles)
{
	FileListHashMap items;
	const auto entries = listDirectoryWithDetails(dirPath);
	if (!entries)
		return items;

	items.reserve(entries->size() + 1); // + 1: [..]
	for (const auto& entry : *entries)
	{
		CFileSystemObject object{ dirPath, entry };
		if ((!object.isFile() && !object.isDir()) || (!showHiddenFiles && object.isHidden()))
			continue; // Could be a socket

		const qulonglong hash = object.hash();
		items.try_emplace(hash, std::move(object));
	}

	if (auto cdUp = CFileSystemObject::cdUpEntryOf(dirPath))
	{
		const qulonglong hash = cdUp->hash();
		items.try_emplace(hash, std::move(*cdUp));
	}

	return items;
}

#pragma once

#include "cfilesystemobject.h"


#include <stdint.h>

// Totals over file list items; [..] is never counted
struct FolderContentsSummary {
	uint64_t numFiles = 0;
	uint64_t numFolders = 0;
	uint64_t size = 0; // A folder counts only once its size has been calculated

	void add(const CFileSystemObject& item)
	{
		if (item.isCdUp())
			return;

		if (item.isFile())
			++numFiles;
		else if (item.isDir())
			++numFolders;

		size += item.size();
	}
};

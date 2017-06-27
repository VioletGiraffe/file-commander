#pragma once

#include "cfilesystemobject.h"

#include <vector>

class CFolderEnumeratorRecursive
{
public:
	CFolderEnumeratorRecursive();

	static void enumerateFolder(const QString& dirPath, std::vector<CFileSystemObject>& result, bool sort = true);
};

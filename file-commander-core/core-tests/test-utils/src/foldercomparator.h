#pragma once

#include <vector>

class CFileSystemObject;

bool compareFolderContents(const std::vector<CFileSystemObject>& source, const std::vector<CFileSystemObject>& dest);

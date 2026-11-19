#pragma once

// Shared by the core and GUI test suites: file list items built in memory.

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <time.h>

// The folder of the items built by name: its rows' listed folder, see fileListFolderPrefix()
inline const QString itemsFolder = QStringLiteral("/folder/");

// An entry of itemsFolder; a test that changes it before listing keeps the properties, the others use makeItem()
inline CFileSystemObjectProperties itemProperties(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	CFileSystemObjectProperties properties;
	properties.type = type;
	const QString fullName = extension.isEmpty() ? name : name + '.' + extension;
	properties.fullPath = itemsFolder + fullName + (type == Directory ? QStringLiteral("/") : QString{});
	properties.size = size;
	properties.modificationTime = modificationTime;
	return properties;
}

inline CFileSystemObject makeItem(FileSystemObjectType type, const QString& name, const QString& extension = {}, uint64_t size = 0, time_t modificationTime = 0)
{
	return CFileSystemObject{ itemProperties(type, name, extension, size, modificationTime) };
}

inline CFileSystemObjectProperties cdUpItemProperties()
{
	CFileSystemObjectProperties properties;
	properties.type = Directory;
	properties.fullPath = QStringLiteral("/");
	properties.isCdUp = true;
	return properties;
}

inline CFileSystemObject makeCdUpItem()
{
	return CFileSystemObject{ cdUpItemProperties() };
}

// An entry anywhere, built in memory. A folder's path ends with '/'.
inline CFileSystemObject makeItemAt(FileSystemObjectType type, const QString& fullPath)
{
	CFileSystemObjectProperties properties;
	properties.type = type;
	properties.fullPath = fullPath;
	return CFileSystemObject{ properties };
}

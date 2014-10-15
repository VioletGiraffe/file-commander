#pragma once

#include "cfilesystemobject.h"

#include "QtCoreIncludes"
#include <vector>
#include <stdint.h>
#include <assert.h>

inline std::vector<CFileSystemObject> recurseDirectoryItems(const QString &dirPath, bool includeFolders)
{
	std::vector<CFileSystemObject> objects;
	if (QFileInfo(dirPath).isDir())
	{
		QDir dir (dirPath);
		assert (dir.exists());
		QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs |  QDir::Hidden | QDir::NoSymLinks | QDir::NoDotAndDotDot | QDir::System);
		for (auto it = list.begin(); it != list.end(); ++it)
		{
			if(it->isDir())
			{
				auto childrenItems = recurseDirectoryItems(it->absoluteFilePath(), includeFolders);
				objects.insert(objects.end(), childrenItems.begin(), childrenItems.end());
				if (includeFolders)
					objects.emplace_back(CFileSystemObject(*it));
			}
			else if (it->isFile())
				objects.emplace_back(CFileSystemObject(*it));
		}
	}
	else
		objects.push_back(CFileSystemObject(QFileInfo(dirPath)));

	return objects;
}

inline QString toNativeSeparators(const QString &path)
{
#ifdef _WIN32
	return QString(path).replace('/', '\\');
#else
	return path;
#endif
}

inline QString toPosixSeparators(const QString &path)
{
#ifdef _WIN32
	return QString(path).replace('\\', '/');
#else
	return path;
#endif
}

inline QString cleanPath(const QString& path)
{
	return QString(path).replace("\\\\", "\\").replace("//", "/");
}

inline QString fileSizeToString(uint64_t size)
{
	const unsigned int KB = 1024;
	const unsigned int MB = 1024 * KB;
	const unsigned int GB = 1024 * MB;
	if (size >= GB)
		return QString("%1 GiB").arg(QString::number(size / float(GB), 'f', 1));
	else if (size >= MB)
		return QString("%1 MiB").arg(QString::number(size / float(MB), 'f', 1));
	else if (size >= KB)
		return QString("%1 KiB").arg(QString::number(size / float(KB), 'f', 1));
	else
		return QString("%1 B").arg(size);
}

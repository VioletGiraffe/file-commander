#pragma once

#include "cfilesystemobject.h"
#include "assert/advanced_assert.h"

#include <assert.h>
#include <stdint.h>
#include <cmath>

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
	assert(!path.contains('\\'));
	return path;
#endif
}

inline QString cleanPath(const QString& path)
{
	return QString(path).replace("\\\\", "\\").replace("//", "/");
}

inline QString fileSizeToString(uint64_t size, const char maxUnit = '\0', const QString& spacer = QString())
{
	const unsigned int KB = 1024;
	const unsigned int MB = 1024 * KB;
	const unsigned int GB = 1024 * MB;

	const std::map<char, unsigned int> unitCodes {{'B', 0}, {'K', KB}, {'M', MB}};
	const unsigned int maxUnitSize = unitCodes.count(maxUnit) > 0 ? unitCodes.at(maxUnit) : std::numeric_limits<unsigned int>::max();

	QString str;
	float n = 0.0f;
	if (size >= GB && maxUnitSize >= GB)
	{
		n = size / float(GB);
		str = QString("%1 GiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= MB && maxUnitSize >= MB)
	{
		n = size / float(MB);
		str = QString("%1 MiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= KB && maxUnitSize >= KB)
	{
		n = size / float(KB);
		str = QString("%1 KiB").arg(QString::number(n, 'f', 1));
	}
	else
	{
		n = (float)size;
		str = QString("%1 B").arg(size);
	}

	if (!spacer.isEmpty() && n > 0.0f)
	{
		for (int spacerPos = (int)std::log10(n) - 3; spacerPos > 0; spacerPos -= 3)
			str.insert(spacerPos + 1, spacer);
	}

	return str;
}

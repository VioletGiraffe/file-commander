#pragma once

#include "cfilesystemobject.h"
#include "assert/advanced_assert.h"
#include "container/std_container_helpers.hpp"
#include "std_helpers/qt_container_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <vector>

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
	assert_debug_only(!path.contains('\\'));
	return path;
#endif
}

inline constexpr char nativeSeparator()
{
#ifdef _WIN32
	return '\\';
#else
	return '/';
#endif
}

inline QString cleanPath(const QString& path)
{
	return QString(path).replace(QStringLiteral("\\\\"), QStringLiteral("\\")).replace(QStringLiteral("//"), QStringLiteral("/"));
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
		str = QStringLiteral("%1 GiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= MB && maxUnitSize >= MB)
	{
		n = size / float(MB);
		str = QStringLiteral("%1 MiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= KB && maxUnitSize >= KB)
	{
		n = size / float(KB);
		str = QStringLiteral("%1 KiB").arg(QString::number(n, 'f', 1));
	}
	else
	{
		n = (float)size;
		str = QStringLiteral("%1 B").arg(size);
	}

	if (!spacer.isEmpty() && n > 0.0f)
	{
		for (int spacerPos = (int)std::log10(n) - 3; spacerPos > 0; spacerPos -= 3)
			str.insert(spacerPos + 1, spacer);
	}

	return str;
}

constexpr bool caseSensitiveFilesystem()
{
#if defined _WIN32
	return false;
#elif defined __APPLE__
	return false;
#elif defined __linux__
	return true;
#elif defined __FreeBSD__
	return true;
#else
#error "Unknown operating system"
	return true;
#endif
}

inline std::vector<QString> pathComponents(const QString& path)
{
	assert_debug_only(!path.contains('\\') && !path.contains("//"));
	auto components = path.split('/', QString::KeepEmptyParts);
	if (components.empty())
		return { path };

	if (components.front().isEmpty())
		components.front() = '/';

	if (components.back().isEmpty())
		components.pop_back();

	return to_vector(std::move(components));
}

// Returns true if this object is a child of parent, either direct or indirect
inline QString longestCommonRootPath(const QString& pathA, const QString& pathB)
{
	if (pathA.compare(pathB, caseSensitiveFilesystem() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0)
		return pathA; // Full match

	const auto hierarchyA = pathComponents(pathA);
	const auto hierarchyB = pathComponents(pathB);

	const auto mismatch = std::mismatch(cbegin_to_end(hierarchyA), cbegin_to_end(hierarchyB), [](const QString& left, const QString& right){
		return left.compare(right, caseSensitiveFilesystem() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
	});

	if (mismatch.first == hierarchyA.cbegin() || mismatch.second == hierarchyB.cbegin())
		return {}; // No common prefix

	// Sanity check
	assert_debug_only(std::distance(mismatch.first, hierarchyA.cbegin()) == std::distance(mismatch.second, hierarchyB.cbegin()) && std::distance(mismatch.first, hierarchyA.cbegin()) <= pathA.size());

	QString result;
	for (auto it = hierarchyA.cbegin(); it != mismatch.first; ++it)
	{
		result += *it;
		result += '/';
	}

	return result;
}

// Returns true if this object is a child of parent, either direct or indirect
inline QString longestCommonRootPath(const CFileSystemObject& object1, const CFileSystemObject& object2)
{
	if (!object1.isValid() || !object2.isValid())
		return {};

	{
		const auto longestCommonRoot = longestCommonRootPath(object1.fullAbsolutePath(), object2.fullAbsolutePath());
		if (!longestCommonRoot.isEmpty())
			return longestCommonRoot;
	}

	if (!object1.isSymLink() && !object2.isSymLink())
		return {};

	const auto resolvedLink1 = object1.isSymLink() ? object1.symLinkTarget() : object1.fullAbsolutePath();
	const auto resolvedLink2 = object2.isSymLink() ? object2.symLinkTarget() : object2.fullAbsolutePath();

	assert_and_return_r(!resolvedLink1.isEmpty() && !resolvedLink2.isEmpty(), {});
	const auto longestCommonRootResolved = longestCommonRootPath(resolvedLink1, resolvedLink2);
	return longestCommonRootResolved;
}

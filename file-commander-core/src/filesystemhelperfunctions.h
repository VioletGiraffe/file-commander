#pragma once

#include "cfilesystemobject.h"
#include "assert/advanced_assert.h"
#include "container/std_container_helpers.hpp"
#include "std_helpers/qt_container_helpers.hpp"

#include <QStringBuilder>

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <vector>

[[nodiscard]] inline constexpr char nativeSeparator() noexcept
{
#ifdef _WIN32
	return '\\';
#else
	return '/';
#endif
}

[[nodiscard]] inline QString toNativeSeparators(QString path)
{
#ifdef _WIN32
	return path.replace('/', nativeSeparator());
#else
	return path;
#endif
}

[[nodiscard]] inline QString toPosixSeparators(QString path)
{
#ifdef _WIN32
	return path.replace(nativeSeparator(), '/');
#else
	assert_debug_only(!path.contains('\\'));
	return path;
#endif
}

[[nodiscard]] inline QString escapedPath(QString path)
{
	if (!path.contains(' ') || path.startsWith('\"'))
		return path;

#ifdef _WIN32
	return '\"' % path % '\"';
#else
	return path.replace(' ', QLatin1String("\\ "));
#endif
}

[[nodiscard]] inline QString cleanPath(QString path)
{
	return path.replace(QStringLiteral("\\\\"), QStringLiteral("\\")).replace(QStringLiteral("//"), QStringLiteral("/"));
}

[[nodiscard]] inline QString fileSizeToString(uint64_t size, const char maxUnit = '\0', const QString& spacer = QString())
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

[[nodiscard]] inline constexpr bool caseSensitiveFilesystem() noexcept
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

[[nodiscard]] inline std::vector<QString> pathComponents(const QString& path)
{
#ifndef _WIN32
	assert_debug_only(!path.contains('\\') && !path.contains("//"));
#else
	// This could be a network path
	assert_debug_only(!path.contains('\\') && path.lastIndexOf("//") <= 0);
#endif // !_WIN32
	auto components = path.split('/', Qt::KeepEmptyParts);
	if (components.empty())
		return { path };

	if (components.front().isEmpty())
		components.front() = '/';

	if (components.back().isEmpty())
		components.pop_back();

	return to_vector(std::move(components));
}

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] inline QString longestCommonRootPath(const QString& pathA, const QString& pathB)
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
		if (*it != '/')
			result += '/';
		else
			assert_debug_only(it->endsWith('/'));
	}

	return result;
}

// Returns true if this object is a child of parent, either direct or indirect
[[nodiscard]] inline QString longestCommonRootPath(const CFileSystemObject& object1, const CFileSystemObject& object2)
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

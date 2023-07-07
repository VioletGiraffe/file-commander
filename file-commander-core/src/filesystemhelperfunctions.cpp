#include "filesystemhelperfunctions.h"
#include "cfilesystemobject.h"

#include "qtcore_helpers/qstring_helpers.hpp"
#include "std_helpers/qt_container_helpers.hpp"

#include "assert/advanced_assert.h"
#include "container/std_container_helpers.hpp"
#include "lang/type_traits_fast.hpp"

DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>
#include <stdint.h>

QString toNativeSeparators(QString path)
{
#ifdef _WIN32
	return path.replace('/', nativeSeparator());
#else
	return path;
#endif
}

QString toPosixSeparators(QString path)
{
#ifdef _WIN32
	return path.replace(nativeSeparator(), '/');
#else
	assert_debug_only(!path.contains('\\'));
	return path;
#endif
}

QString escapedPath(QString path)
{
	if (!path.contains(' '))
		return path;

#ifdef _WIN32
	static constexpr char quoteCharacter = '\"';
#else
	static constexpr char quoteCharacter = '\'';
#endif

	assert_debug_only(!path.endsWith(nativeSeparator()));
	assert_and_return_r(!path.startsWith(quoteCharacter), path); // Already escaped!
	path.reserve(path.size() + 2);
	path.prepend(quoteCharacter).append(quoteCharacter);
	return path;
}

QString cleanPath(QString path)
{
	return path.replace(QStringLiteral(R"(\\)"), QStringLiteral(R"(\)")).replace(QStringLiteral("//"), QStringLiteral("/"));
}

QString fileSizeToString(uint64_t size, const char maxUnit, const QString &spacer)
{
	static constexpr uint64_t KB = 1024;
	static constexpr uint64_t MB = 1024 * KB;
	static constexpr uint64_t GB = 1024 * MB;

	const uint64_t maxUnitSize = [maxUnit]() -> uint64_t {
		switch(maxUnit) {
		case 'B': return 0;
		case 'K': return KB;
		case 'M': return MB;
		default:  return uint64_max;
		}
	}();

	QString str;
	float n = 0.0f;
	if (size >= GB && maxUnitSize >= GB)
	{
		n = (float)size / float(GB);
		str = QStringLiteral("%1 GiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= MB && maxUnitSize >= MB)
	{
		n = (float)size / float(MB);
		str = QStringLiteral("%1 MiB").arg(QString::number(n, 'f', 1));
	}
	else if (size >= KB && maxUnitSize >= KB)
	{
		n = (float)size / float(KB);
		str = QStringLiteral("%1 KiB").arg(QString::number(n, 'f', 1));
	}
	else
	{
		n = (float)size;
		str = QStringLiteral("%1 B").arg(size);
	}

	if (!spacer.isEmpty() && n > 0.0f)
	{
		for (int spacerPos = (int)std::log10(n) - 3; spacerPos >= 0; spacerPos -= 3)
			str.insert(spacerPos + 1, spacer);
	}

	return str;
}

std::vector<QString> pathComponents(const QString &path)
{
#ifndef _WIN32
	assert_debug_only(!path.contains('\\') && !path.contains("//"));
#else
	// This could be a network path
	assert_debug_only(!path.contains('\\') && path.lastIndexOf(QSL("//")) <= 0);
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
QString longestCommonRootPath(const QString &pathA, const QString &pathB)
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
QString longestCommonRootPath(const CFileSystemObject &object1, const CFileSystemObject &object2)
{
	if (!object1.isValid() || !object2.isValid())
		return {};

	{
		auto longestCommonRoot = longestCommonRootPath(object1.fullAbsolutePath(), object2.fullAbsolutePath());
		if (!longestCommonRoot.isEmpty())
			return longestCommonRoot;
	}

	if (!object1.isSymLink() && !object2.isSymLink())
		return {};

	const auto resolvedLink1 = object1.isSymLink() ? object1.symLinkTarget() : object1.fullAbsolutePath();
	const auto resolvedLink2 = object2.isSymLink() ? object2.symLinkTarget() : object2.fullAbsolutePath();

	assert_and_return_r(!resolvedLink1.isEmpty() && !resolvedLink2.isEmpty(), {});
	auto longestCommonRootResolved = longestCommonRootPath(resolvedLink1, resolvedLink2);
	return longestCommonRootResolved;
}

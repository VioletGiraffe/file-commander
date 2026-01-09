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

QString fileSizeToString(uint64_t size, const char maxUnit, const QString& spacer, int significantPlaces)
{
	static constexpr uint64_t KB = 1024ULL;
	static constexpr uint64_t MB = 1024ULL * KB;
	static constexpr uint64_t GB = 1024ULL * MB;
	static constexpr uint64_t TB = 1024ULL * GB;
	static constexpr uint64_t PB = 1024ULL * TB;
	static constexpr uint64_t EB = 1024ULL * PB;

	struct Unit { uint64_t threshold; const char* label; };
	static constexpr Unit units[]{
		{ EB, "EiB" },
		{ PB, "PiB" },
		{ TB, "TiB" },
		{ GB, "GiB" },
		{ MB, "MiB" },
		{ KB, "KiB" },
		{ 0ULL, "B" }
	};

	const char maxUnitUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(maxUnit)));

	const uint64_t maxUnitSize = [maxUnitUpper]() -> uint64_t {
		if (maxUnitUpper == 'B')
			return 0ULL; // bytes-only sentinel
		for (const auto& u : units) {
			if (u.label[0] == maxUnitUpper)
				return u.threshold;
		}
		return uint64_max;
	}();

	// First pass: try to satisfy significantPlaces (if non-zero)
	const Unit* chosen = nullptr;
	if (significantPlaces > 0)
	{
		// iterate from smallest to largest unit
		for (int i = std::size(units) - 1; i >= 0; --i)
		{
			const auto& u = units[i];

			if (maxUnitSize < u.threshold)
				continue;

			if (u.threshold != 0ULL)
			{
				if (size < u.threshold)
					continue;
			}

			const uint64_t whole = (u.threshold == 0ULL) ? size : (size / u.threshold);

			int digits = 1;
			for (uint64_t tmp = whole; tmp >= 10; tmp /= 10, ++digits) {}

			if (digits <= significantPlaces)
			{
				chosen = &u;
				break; // smallest unit that fits
			}
		}
	}

	// Fallback: only use the unit threshold if none was chosen above (or significantPlaces == 0)
	if (!chosen)
	{
		for (const auto& u : units)
		{
			if (maxUnitSize < u.threshold)
				continue;
			if (u.threshold == 0ULL || size >= u.threshold)
			{
				chosen = &u;
				break;
			}
		}
	}

	// Safety: if still not chosen (shouldn't happen), use bytes
	if (!chosen)
	{
		chosen = &units[6]; // bytes entry
	}

	// Build numeric string separately so we can insert the spacer into the whole part only
	QString numeric;
	if (chosen->threshold == 0ULL)
	{
		numeric = QString::number(size);
	}
	else
	{
		double n = static_cast<double>(size) / static_cast<double>(chosen->threshold);
		numeric = QString::number(n, 'f', 1);
	}

	// Insert thousands spacer into whole part only
	if (!spacer.isEmpty())
	{
		const qsizetype dotPos = numeric.indexOf('.');
		const qsizetype wholeLen = (dotPos == -1) ? numeric.size() : dotPos;
		for (qsizetype pos = wholeLen - 3; pos > 0; pos -= 3)
			numeric.insert(pos, spacer);
	}

	QString result;
	if (chosen->threshold == 0ULL)
	{
		result = QStringLiteral("%1 B").arg(numeric);
	}
	else
	{
		result = QStringLiteral("%1 %2").arg(numeric, QString::fromLatin1(chosen->label));
	}

	return result;
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

#include "centrypath.h"
#include "filesystemhelperfunctions.h"

#include "assert/advanced_assert.h"
#include "lang/utils.hpp"

DISABLE_COMPILER_WARNINGS
#include <QStringBuilder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace
{

#ifdef _WIN32
[[nodiscard]] bool isAsciiLetter(const QChar c) noexcept
{
	return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z'));
}
#endif

// Length of the root prefix of a normalized path: "/" -> 1, "C:/" -> 3, "//server/share..." -> through the share name.
qsizetype rootLength(const QString& path)
{
#ifdef _WIN32
	if (path.startsWith(QLatin1String("//")))
	{
		const qsizetype serverEnd = path.indexOf(QLatin1Char('/'), 2);
		assert_r(serverEnd > 2);
		const qsizetype shareEnd = path.indexOf(QLatin1Char('/'), serverEnd + 1);
		return shareEnd == -1 ? path.length() : shareEnd;
	}
	return 3; // "C:/"
#else
	return 1;
#endif
}

} // namespace

CEntryPath::CEntryPath(QString normalizedPath) noexcept :
	_path{ mv(normalizedPath) }
{
}

const QString& CEntryPath::value() const noexcept
{
	return _path;
}

bool CEntryPath::isRoot() const
{
	return _path.length() == rootLength(_path);
}

CEntryPath CEntryPath::parent() const
{
	assert_r(!isRoot());
	const qsizetype lastSeparator = _path.lastIndexOf(QLatin1Char('/'));
	// The root's own separator (if any) is part of the root spelling, not a component boundary to cut at.
	return CEntryPath{ _path.left(std::max(lastSeparator, rootLength(_path))) };
}

CEntryPath CEntryPath::child(const QString& name) const
{
	assert_r(isValidEntryName(name));
	return _path.endsWith(QLatin1Char('/')) ? CEntryPath{ _path % name } : CEntryPath{ _path % QLatin1Char('/') % name };
}

QString CEntryPath::name() const
{
	if (isRoot())
		return _path;
	return _path.mid(_path.lastIndexOf(QLatin1Char('/')) + 1);
}

bool CEntryPath::operator==(const CEntryPath& other) const noexcept
{
	return _path.compare(other._path, caseSensitiveFilesystem() ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
}

std::optional<CEntryPath> parseOperationPath(QString path)
{
	path = path.trimmed();
	if (path.isEmpty())
		return {};

	QString root;
	QString rest;

#ifdef _WIN32
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));

	if (path.startsWith(QLatin1String("//")) && path.length() > 2 && path[2] != QLatin1Char('/'))
	{
		const qsizetype serverEnd = path.indexOf(QLatin1Char('/'), 2);
		if (serverEnd == -1) // "//server" alone is not a filesystem path
			return {};

		qsizetype shareEnd = path.indexOf(QLatin1Char('/'), serverEnd + 1);
		if (shareEnd == -1)
			shareEnd = path.length();

		const QString server = path.mid(2, serverEnd - 2);
		const QString share = path.mid(serverEnd + 1, shareEnd - serverEnd - 1);
		if (share.isEmpty() || server == QLatin1String(".") || server == QLatin1String("..")
			|| share == QLatin1String(".") || share == QLatin1String(".."))
			return {};

		root = QLatin1String("//") % server % QLatin1Char('/') % share;
		rest = path.mid(shareEnd);
	}
	else if (path.length() >= 3 && isAsciiLetter(path[0]) && path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/'))
	{
		// "C:" without a separator names the drive's current directory, not its root - rejected with everything relative.
		root = path[0].toUpper() % QLatin1String(":/");
		rest = path.mid(3);
	}
	else
		return {};
#else
	if (path[0] != QLatin1Char('/'))
		return {};

	root = QLatin1Char('/');
	rest = path.mid(1);
#endif

	QStringList components;
	for (const auto& part : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts))
	{
		if (part == QLatin1String("."))
			continue;

		if (part == QLatin1String(".."))
		{
			if (!components.isEmpty()) // ".." at the root clamps, like the OS itself resolves "/.."
				components.removeLast();
			continue;
		}

		components.push_back(part);
	}

	if (components.isEmpty())
		return CEntryPath{ mv(root) };

	if (!root.endsWith(QLatin1Char('/')))
		root += QLatin1Char('/');
	root += components.join(QLatin1Char('/'));
	return CEntryPath{ mv(root) };
}

bool isValidEntryName(const QString& name)
{
	return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
		&& name != QLatin1String(".") && name != QLatin1String("..");
}

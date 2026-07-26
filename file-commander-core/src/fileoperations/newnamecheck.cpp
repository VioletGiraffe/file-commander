#include "newnamecheck.h"
#include "centrypath.h"

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

NameRejection checkNewEntryName(const QString& name)
{
	if (name.isEmpty())
		return NameRejection::Empty;

	if (std::all_of(name.cbegin(), name.cend(), [](const QChar c) { return c.isSpace(); }))
		return NameRejection::WhitespaceOnly;

	// isSingleComponentName owns the structural rules; which of them failed matters only for the wording.
	if (!isSingleComponentName(name))
	{
		return name == QLatin1String(".") || name == QLatin1String("..")
			? NameRejection::DotComponent
			: NameRejection::Separator;
	}

#ifdef _WIN32
	// Legal on NTFS and reachable through the extended-length prefix this app uses, but every tool that goes through
	// Win32 normalization - Explorer, cmd, most applications - would resolve the name without them and miss the entry.
	if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
		return NameRejection::TrailingDotOrSpace;
#endif

	return NameRejection::None;
}

NameRejection checkNewEntryPath(const QString& relativePath)
{
	QString path = relativePath;
#ifdef _WIN32
	path.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif

	const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
	if (components.isEmpty())
		return NameRejection::Empty;

	for (const QString& component : components)
	{
		if (const NameRejection rejection = checkNewEntryName(component); rejection != NameRejection::None)
			return rejection;
	}

	return NameRejection::None;
}

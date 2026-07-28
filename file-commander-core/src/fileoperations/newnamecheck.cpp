#include "newnamecheck.h"
#include "centrypath.h"

DISABLE_COMPILER_WARNINGS
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace
{

#ifdef _WIN32
bool isWindowsInvalidNameCharacter(const QChar c)
{
	if (c.unicode() < 0x20)
		return true;

	switch (c.unicode())
	{
	case '<':
	case '>':
	case ':':
	case '"':
	case '|':
	case '?':
	case '*': return true;
	default: return false;
	}
}

bool isWindowsReservedDeviceName(const QString& name)
{
	const qsizetype dotPosition = name.indexOf(QLatin1Char('.'));
	const QString stem = dotPosition == -1 ? name : name.left(dotPosition);

	if (stem.compare(QLatin1String("CON"), Qt::CaseInsensitive) == 0
		|| stem.compare(QLatin1String("PRN"), Qt::CaseInsensitive) == 0
		|| stem.compare(QLatin1String("AUX"), Qt::CaseInsensitive) == 0
		|| stem.compare(QLatin1String("NUL"), Qt::CaseInsensitive) == 0)
		return true;

	if (stem.length() != 4 || stem[3] < QLatin1Char('1') || stem[3] > QLatin1Char('9'))
		return false;

	const QString prefix = stem.left(3);
	return prefix.compare(QLatin1String("COM"), Qt::CaseInsensitive) == 0
		|| prefix.compare(QLatin1String("LPT"), Qt::CaseInsensitive) == 0;
}
#endif

} // namespace

NameRejection checkNewEntryName(const QString& name)
{
	if (name.isEmpty())
		return NameRejection::Empty;

	if (name.contains(QChar{ 0 }))
		return NameRejection::InvalidCharacter;

#ifdef _WIN32
	if (std::any_of(name.cbegin(), name.cend(), isWindowsInvalidNameCharacter))
		return NameRejection::InvalidCharacter;
#endif

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

	if (isWindowsReservedDeviceName(name))
		return NameRejection::ReservedDeviceName;
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

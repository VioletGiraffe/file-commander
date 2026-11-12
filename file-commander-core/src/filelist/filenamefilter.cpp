#include "filenamefilter.h"


DISABLE_COMPILER_WARNINGS
#include <QLatin1StringView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

[[nodiscard]] static QString nameFilterToRegex(QStringView filter)
{
	const bool anchorStart = filter.startsWith('^');
	if (anchorStart)
		filter = filter.sliced(1);

	const bool anchorEnd = filter.endsWith('$');
	if (anchorEnd)
		filter.chop(1);

	// Escaping everything and restoring only our own two wildcards is what keeps '.', '+' and brackets literal.
	// Every '*' in the escaped text is preceded by the backslash that escaped it, so neither replacement can match
	// across two escape sequences.
	QString pattern = QRegularExpression::escape(filter);
	pattern.replace(QLatin1StringView{ "\\*" }, QLatin1StringView{ ".*" });
	pattern.replace(QLatin1StringView{ "\\?" }, QLatin1StringView{ "." });

	// \A and \z rather than ^ and $: '$' also matches immediately before a trailing newline, which a POSIX name may end with.
	if (anchorStart)
		pattern.prepend(QLatin1StringView{ "\\A" });
	if (anchorEnd)
		pattern.append(QLatin1StringView{ "\\z" });

	return pattern;
}

QStringList splitNameFilterField(const QString& fieldText)
{
	QStringList filters;
	for (const QStringView part : QStringView{ fieldText }.split(u';'))
	{
		if (const QStringView filter = part.trimmed(); !filter.isEmpty())
			filters.push_back(filter.toString());
	}

	return filters;
}

FileNameFilter::FileNameFilter(const QStringList& filters, Qt::CaseSensitivity caseSensitivity)
{
	// The filters are alternatives, so one that matches every name makes name matching redundant altogether
	if (std::ranges::any_of(filters, [](const QString& filter) { return filter == '*'; }))
		return;

	// Our wildcards stand for any character, including the newline a POSIX name may contain
	QRegularExpression::PatternOptions patternOptions = QRegularExpression::DotMatchesEverythingOption;
	if (caseSensitivity == Qt::CaseInsensitive)
		patternOptions |= QRegularExpression::CaseInsensitiveOption;

	_filters.reserve((size_t)filters.size());
	for (const QString& filter : filters)
		_filters.emplace_back(nameFilterToRegex(filter), patternOptions);
}

bool FileNameFilter::matches(QStringView name) const
{
	return _filters.empty() || std::ranges::any_of(_filters, [name](const QRegularExpression& filter) { return filter.matchView(name).hasMatch(); });
}

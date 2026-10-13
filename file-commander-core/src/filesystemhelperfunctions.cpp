#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "directoryscan/thinioqstring.h"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>

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
	return path; // A backslash is an ordinary character in a name here, not a separator to convert
#endif
}

// Unicode letters and digits are never special to a shell, so paths in any script stay unquoted.
static bool isSafeUnquoted(const QChar c) noexcept
{
	if (c.isLetterOrNumber())
		return true;

	switch (c.unicode())
	{
	case '.': case '_': case '-': case '+': case '@': case '/':
#ifdef _WIN32
	case '\\': case ':':
#endif
		return true;
	default:
		return false;
	}
}

QString shellQuotedPath(QString path)
{
	path = withoutTrailingSeparator(std::move(path));

	if (std::all_of(path.begin(), path.end(), isSafeUnquoted))
		return path;

#ifdef _WIN32
	assert_and_return_r(!path.contains('\"'), path); // Illegal in a Windows path, so the caller quoted this already
	return '\"' % path % '\"';
#else
	path.replace('\'', QSL("'\\''")); // Single quotes protect every other character; a single quote must be closed out, escaped, then reopened
	return '\'' % path % '\'';
#endif
}

#ifndef _WIN32
std::optional<QStringList> splitShellWords(const QString& text)
{
	QStringList words;
	QString word;
	bool inWord = false; // Set by a quote too: '' is an empty word
	enum class Quote { None, Single, Double } quote = Quote::None;

	for (qsizetype i = 0; i < text.size(); ++i)
	{
		const QChar c = text[i];
		if (quote == Quote::Single)
		{
			if (c == '\'')
				quote = Quote::None;
			else
				word += c;
		}
		else if (c == '\\')
		{
			if (++i == text.size())
				return std::nullopt;

			const QChar escaped = text[i];
			if (escaped == '\n')
				continue; // Line continuation

			// Inside double quotes a backslash escapes only these, and is literal before anything else
			static constexpr QStringView escapableInDoubleQuotes = u"$`\"\\";
			if (quote == Quote::Double && !escapableInDoubleQuotes.contains(escaped))
				word += '\\';

			word += escaped;
			inWord = true;
		}
		else if (quote == Quote::Double)
		{
			if (c == '\"')
				quote = Quote::None;
			else
				word += c;
		}
		else if (c == '\'' || c == '\"')
		{
			quote = c == '\'' ? Quote::Single : Quote::Double;
			inWord = true;
		}
		else if (c == ' ' || c == '\t' || c == '\n')
		{
			if (inWord)
				words.push_back(std::exchange(word, {}));
			inWord = false;
		}
		else
		{
			word += c;
			inWord = true;
		}
	}

	if (quote != Quote::None)
		return std::nullopt;

	if (inWord)
		words.push_back(std::move(word));

	return words;
}
#endif

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "directoryscan/thinioqstring.h"
#include "paths/pathsyntax.h"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QByteArray>
#include <QDebug>
#include <QString>
#include <QStringBuilder>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#else
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wordexp.h>
#endif

#include <algorithm>
#include <iterator>
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

static QString expandEnvironmentVariables(const QString& string)
{
#ifdef _WIN32
	if (!string.contains('%'))
		return string;

	WCHAR source[16384 + 1];
	WCHAR result[16384 + 1];

	static_assert (sizeof(WCHAR) == 2);
	const auto length = string.toWCharArray(source);
	source[length] = 0;
	if (const auto resultLength = ExpandEnvironmentStringsW(source, result, static_cast<DWORD>(std::size(result))); resultLength > 0)
		return toPosixSeparators(QString::fromWCharArray(result, (int)resultLength - 1));
	else
		return string;
#else
	QString result = string;
	if (result == '~' || result.startsWith(QSL("~/")))
		result.replace(0, 1, QString::fromUtf8(getenv("HOME")));

	if (result.contains('$'))
	{
		wordexp_t words{};
		// WRDE_NOCMD: a command substitution in the text fails the expansion instead of running
		// WRDE_UNDEF: an undefined variable fails it too and leaves the text as typed, matching Windows
		const int status = wordexp(result.toUtf8().constData(), &words, WRDE_NOCMD | WRDE_UNDEF);
		if (status == 0)
		{
			QStringList expandedWords;
			for (size_t i = 0; i < words.we_wordc; ++i)
				expandedWords.push_back(QString::fromUtf8(words.we_wordv[i]));

			// Field splitting cut the text at its blanks: a run of blanks comes back as one space
			result = expandedWords.join(' ');
		}

		// Only these two results leave memory to free
		if (status == 0 || status == WRDE_NOSPACE)
			wordfree(&words);
	}

	return result;
#endif
}

QString resolveUserPath(const QString& text, const QString& baseDir)
{
	assert_r(absoluteRootLength(baseDir) > 0);

	QString path = expandEnvironmentVariables(text);
	if (path.isEmpty())
		return {};

#ifdef _WIN32
	path.replace('\\', '/');
	const char16_t driveLetter = path[0].toUpper().unicode();
	if (path.size() >= 2 && driveLetter >= u'A' && driveLetter <= u'Z' && path[1] == ':' && (path.size() == 2 || path[2] != '/'))
		path.insert(2, '/'); // Not the drive's process-wide current directory
	else if (path.startsWith(QSL("//")) && absoluteRootLength(path) == 0)
		return {};
	else if (path.startsWith('/') && !path.startsWith(QSL("//")))
	{
		QStringView baseRoot = QStringView{ baseDir }.first(absoluteRootLength(baseDir));
		if (baseRoot.endsWith('/'))
			baseRoot.chop(1);
		path.prepend(baseRoot);
	}
#endif

	if (absoluteRootLength(path) == 0)
		path = baseDir % '/' % path;

	return normalizedAbsolutePath(path);
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

std::optional<uint64_t> deviceIdForPath(const QString& fullPath)
{
	// stat() only succeeds on an existing path. If this path doesn't exist yet (e.g. a copy/move destination that will be
	// created), walk up to the nearest existing ancestor: the path will be created on that same device / filesystem.
	QByteArray path = fullPath.toUtf8();
	struct stat info;
	while (!path.isEmpty())
	{
		if (stat(path.constData(), &info) == 0)
		{
			return static_cast<uint64_t>(info.st_dev);
		}

		if (errno != ENOENT)
		{
			qInfo() << __FUNCTION__ << "Failed to query device ID for" << fullPath << strerror(errno);
			break;
		}

		if (path == "/")
			break; // Reached the filesystem root and even it doesn't stat - give up

		const auto lastSlash = path.lastIndexOf('/');
		if (lastSlash < 0)
			break; // No parent component left (shouldn't happen for an absolute path)

		path.truncate(lastSlash == 0 ? 1 : lastSlash); // Keep the leading '/' when the parent is the root itself
	}

	return {};
}
#endif

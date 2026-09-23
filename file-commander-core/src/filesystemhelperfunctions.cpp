#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "container/std_container_helpers.hpp"
#include "fs.hpp" // thin_io
#include "lang/type_traits_fast.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"
#include "std_helpers/qt_container_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <utility>

#ifdef _WIN32
[[nodiscard]] static std::wstring nativePath(const QString& path)
{
	return path.toStdWString();
}
#else
[[nodiscard]] static QByteArray nativePath(const QString& path)
{
	return QFile::encodeName(path);
}
#endif

std::optional<thin_io::entry_identity> resolvedObjectId(const QString& path)
{
	// thin_io prefixes with \\?\, which turns off the normalization that would otherwise absorb a trailing separator.
	const auto metadata = thin_io::get_entry_metadata(nativePath(withoutTrailingSeparator(path)).data(), thin_io::link_behavior::follow);
	if (!metadata)
		return {};

	return metadata->identity;
}

thin_io::filesystem_result<std::vector<thin_io::directory_entry>> listDirectoryWithDetails(const QString& dirPath)
{
	return thin_io::list_directory(nativePath(dirPath).data(), thin_io::listing_detail::full);
}

thin_io::filesystem_result<thin_io::directory_entry> getDirectoryEntry(const QString& path)
{
	return thin_io::get_directory_entry(nativePath(path).data());
}

QString nativeNameToQString(const thin_io::native_string& name)
{
#ifdef _WIN32
	return QString::fromStdWString(name);
#else
	return QFile::decodeName(QByteArray::fromRawData(name.data(), static_cast<qsizetype>(name.size())));
#endif
}

bool isLinkEntry(const thin_io::entry_attributes& attributes) noexcept
{
#ifdef _WIN32
	return attributes.is_link && IsReparseTagNameSurrogate(attributes.reparse_tag);
#else
	return attributes.is_link;
#endif
}

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

QString withoutTrailingSeparator(QString path)
{
	if ((path.endsWith('/') || path.endsWith(nativeSeparator())) && !QDir{ path }.isRoot())
		path.chop(1);

	return path;
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
	assert_debug_only(!path.contains("//"));
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

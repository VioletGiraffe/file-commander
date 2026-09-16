#include "ccsvparser.h"

#include <algorithm>
#include <array>
#include <utility>

QStringView CsvTable::cell(size_t row, size_t column) const noexcept
{
	const size_t firstCell = rowStarts[row], endCell = rowStarts[row + 1];
	if (column >= endCell - firstCell)
		return {};

	const CsvCell& c = cells[firstCell + column];
	return QStringView{ text }.mid(c.offset, c.length);
}

// Calls onLine(QStringView line) for each non-blank line, until it returns false
template <typename Fn>
static void forEachNonBlankLine(QStringView text, Fn&& onLine)
{
	// CRLF splits into a line and a blank one, which is skipped
	for (auto lineStart = text.begin(); lineStart < text.end();)
	{
		const auto lineEnd = std::find_if(lineStart, text.end(), [](QChar c) { return c == u'\r' || c == u'\n'; });
		const QStringView line{ lineStart, lineEnd };
		lineStart = lineEnd == text.end() ? lineEnd : lineEnd + 1;
		if (!line.trimmed().isEmpty() && !onLine(line))
			return;
	}
}

bool csvHasCommentLines(QStringView text)
{
	size_t commentLines = 0, dataLines = 0;
	bool commentsAreLeadingBlock = true;
	bool dataContainsHash = false;
	forEachNonBlankLine(text, [&](QStringView line) {
		if (line.startsWith(commentPrefix))
		{
			++commentLines;
			if (dataLines > 0)
				commentsAreLeadingBlock = false;
		}
		else if (line.contains(commentPrefix))
		{
			dataContainsHash = true;
			return false;
		}
		else
			++dataLines;

		return true;
	});

	return !dataContainsHash && commentLines > 0 && dataLines > 0 && (commentsAreLeadingBlock || commentLines * 10 <= dataLines);
}

QChar detectCsvDelimiter(QStringView text, bool skipCommentLines)
{
	static constexpr std::array candidates{ u',', u';', u'\t', u'|' };
	constexpr int sampleLines = 20;

	struct Stats {
		int firstLineCount = -1;
		int total = 0;
		bool consistent = true;
	};
	std::array<Stats, candidates.size()> stats{};

	int lines = 0;
	forEachNonBlankLine(text, [&](QStringView line) {
		if (skipCommentLines && line.startsWith(commentPrefix))
			return true;

		++lines;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			const int count = static_cast<int>(std::count(line.begin(), line.end(), QChar{ candidates[i] }));
			Stats& s = stats[i];
			if (s.firstLineCount < 0)
				s.firstLineCount = count;
			else if (count != s.firstLineCount)
				s.consistent = false;
			s.total += count;
		}

		return lines < sampleLines;
	});

	const auto rank = [&stats](size_t index) { return std::pair{ stats[index].total > 0 && stats[index].consistent, stats[index].total }; };
	size_t best = 0;
	for (size_t i = 1; i < candidates.size(); ++i)
	{
		if (rank(i) > rank(best))
			best = i;
	}

	return stats[best].total > 0 ? QChar{ candidates[best] } : QChar{ u',' };
}

CsvTable parseCsv(QString text, QChar delimiter, bool skipCommentLines)
{
	static constexpr QChar quote = u'"', cr = u'\r', lf = u'\n';

	CsvTable table;
	table.text = std::move(text);
	QChar* const begin = table.text.data();
	QChar* const end = begin + table.text.size();

	QChar* rp = begin; // read cursor
	// CRLF, LF, lone CR, or the end of input
	const auto skipLineBreak = [&rp, end] {
		if (rp < end && *rp == cr)
			++rp;
		if (rp < end && *rp == lf)
			++rp;
	};

	while (rp < end)
	{
		if (skipCommentLines && *rp == commentPrefix)
		{
			rp = std::find_if(rp, end, [](QChar c) { return c == cr || c == lf; });
			skipLineBreak();
			++table.commentLineCount;
			continue;
		}

		const QChar* const rowBegin = rp;
		const size_t rowStart = table.cells.size();
		table.rowStarts.push_back(rowStart);

		for (;;)
		{
			QChar* fieldStart = rp;
			QChar* wp = rp; // write cursor, never ahead of rp
			if (rp < end && *rp == quote)
			{
				fieldStart = wp = ++rp;
				while (rp < end)
				{
					if (*rp != quote)
					{
						*wp++ = *rp++;
						continue;
					}

					++rp;
					if (rp < end && *rp == quote)
						*wp++ = *rp++; // "" inside quotes
					else
						break; // The closing quote; a missing one runs the field to the end of input, as the spec implies
				}

				// Text between the closing quote and the delimiter is malformed input; kept
				while (rp < end && *rp != delimiter && *rp != cr && *rp != lf)
					*wp++ = *rp++;
			}
			else
			{
				while (rp < end && *rp != delimiter && *rp != cr && *rp != lf)
					++rp;
				wp = rp;
			}

			table.cells.push_back({ fieldStart - begin, wp - fieldStart });

			if (rp < end && *rp == delimiter)
				++rp;
			else
				break;
		}

		// Blank means nothing consumed: a line holding only "" is a row with one empty value
		const bool isBlankLine = rp == rowBegin;
		skipLineBreak();

		if (isBlankLine)
		{
			table.cells.pop_back();
			table.rowStarts.pop_back();
		}
		else
			table.columnCount = std::max(table.columnCount, table.cells.size() - rowStart);
	}

	table.rowStarts.push_back(table.cells.size());
	return table;
}

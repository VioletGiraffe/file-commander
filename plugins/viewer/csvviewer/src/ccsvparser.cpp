#include "ccsvparser.h"

#include <algorithm>
#include <array>
#include <map>
#include <span>
#include <utility>

QStringView CsvTable::cell(size_t row, size_t column) const noexcept
{
	const size_t firstCell = rowStarts[row], endCell = rowStarts[row + 1];
	if (column >= endCell - firstCell)
		return {};

	const CsvCell& c = cells[firstCell + column];
	return QStringView{ text }.mid(c.offset, c.length);
}

// The characters a field may end at, so that a quote right after one opens a quoted field. Only for scanning text
// whose delimiter is not known yet; everything else passes the delimiter alone.
static constexpr std::array<QChar, 4> delimiterCandidates{ u',', u';', u'\t', u'|' };

// Calls onChar(qsizetype index, QChar c) for each character outside a quoted field, until it returns false.
// As in parseCsv, a quote opens a field only where a field can start, and "" inside one is an escape.
template <typename Fn>
static void forEachCharOutsideQuotes(QStringView text, std::span<const QChar> separators, Fn&& onChar)
{
	bool atFieldStart = true;
	for (qsizetype i = 0; i < text.size(); ++i)
	{
		const QChar c = text[i];
		if (atFieldStart && c == u'"')
		{
			for (++i; i < text.size(); ++i)
			{
				if (text[i] != u'"')
					continue;
				if (i + 1 < text.size() && text[i + 1] == u'"')
					++i; // The escape, whose second quote must not close the field
				else
					break;
			}

			atFieldStart = false;
			continue;
		}

		if (!onChar(i, c))
			return;

		atFieldStart = c == u'\r' || c == u'\n' || std::ranges::find(separators, c) != separators.end();
	}
}

// Occurrences of ch that are not inside a quoted field
[[nodiscard]] static qsizetype countOutsideQuotes(QStringView text, QChar ch, std::span<const QChar> separators)
{
	qsizetype count = 0;
	forEachCharOutsideQuotes(text, separators, [&count, ch](qsizetype, QChar c) {
		if (c == ch)
			++count;
		return true;
	});

	return count;
}

// Calls onLine(QStringView line) for each non-blank line, until it returns false. A line break inside a quoted
// field belongs to the field, as in parseCsv, so such a line carries its own breaks.
template <typename Fn>
static void forEachNonBlankLine(QStringView text, std::span<const QChar> separators, Fn&& onLine)
{
	qsizetype lineStart = 0;
	bool wantMore = true;
	// CRLF ends a line and opens a blank one, which is skipped
	forEachCharOutsideQuotes(text, separators, [&](qsizetype index, QChar c) {
		if (c != u'\r' && c != u'\n')
			return true;

		const QStringView line = text.sliced(lineStart, index - lineStart);
		lineStart = index + 1;
		if (!line.trimmed().isEmpty())
			wantMore = onLine(line);
		return wantMore;
	});

	if (wantMore && lineStart < text.size())
	{
		const QStringView lastLine = text.sliced(lineStart);
		if (!lastLine.trimmed().isEmpty())
			onLine(lastLine);
	}
}

bool csvHasCommentLines(QStringView text)
{
	const QChar delimiter = detectCsvDelimiter(text, true);
	const std::span<const QChar> separators{ &delimiter, 1 };

	size_t dataLines = 0;
	bool commentsAreLeadingBlock = true;
	bool dataContainsHash = false;
	std::vector<qsizetype> delimiterCountByCommentLine;
	std::map<qsizetype, size_t> dataLinesByDelimiterCount;
	forEachNonBlankLine(text, separators, [&](QStringView line) {
		if (line.startsWith(commentPrefix))
		{
			delimiterCountByCommentLine.push_back(countOutsideQuotes(line, delimiter, separators));
			if (dataLines > 0)
				commentsAreLeadingBlock = false;
		}
		else if (line.contains(commentPrefix))
		{
			dataContainsHash = true;
			return false;
		}
		else
		{
			++dataLines;
			++dataLinesByDelimiterCount[countOutsideQuotes(line, delimiter, separators)];
		}

		return true;
	});

	const size_t commentLines = delimiterCountByCommentLine.size();
	if (dataContainsHash || commentLines == 0 || dataLines == 0)
		return false;

	if (commentsAreLeadingBlock || commentLines * 10 <= dataLines)
		return true;

	const qsizetype dataDelimiterCount = std::ranges::max_element(dataLinesByDelimiterCount, {}, &std::pair<const qsizetype, size_t>::second)->first;
	const auto misshapenComments = std::ranges::count_if(delimiterCountByCommentLine, [dataDelimiterCount](qsizetype count) { return count != dataDelimiterCount; });
	return static_cast<size_t>(misshapenComments) * 2 > commentLines;
}

QChar detectCsvDelimiter(QStringView text, bool skipCommentLines)
{
	static constexpr auto& candidates = delimiterCandidates;
	constexpr int sampleLines = 20;

	struct Stats {
		int firstLineCount = -1;
		int total = 0;
		bool consistent = true;
	};
	std::array<Stats, candidates.size()> stats{};

	int lines = 0;
	forEachNonBlankLine(text, candidates, [&](QStringView line) {
		if (skipCommentLines && line.startsWith(commentPrefix))
			return true;

		++lines;
		for (size_t i = 0; i < candidates.size(); ++i)
		{
			const int count = static_cast<int>(countOutsideQuotes(line, candidates[i], candidates));
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

CsvTable parseCsv(QString text, QChar delimiter, bool recognizeCommentLines)
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
		if (recognizeCommentLines && *rp == commentPrefix)
		{
			QChar* const lineEnd = std::find_if(rp, end, [](QChar c) { return c == cr || c == lf; });
			table.rowStarts.push_back(table.cells.size());
			table.cells.push_back({ rp - begin, lineEnd - rp });
			table.isCommentRow.push_back(true);
			++table.commentRowCount;

			rp = lineEnd;
			skipLineBreak();
			continue;
		}

		const QChar* const rowBegin = rp;
		const size_t rowStart = table.cells.size();
		table.rowStarts.push_back(rowStart);
		table.isCommentRow.push_back(false);

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
			table.isCommentRow.pop_back();
		}
		else
			table.columnCount = std::max(table.columnCount, table.cells.size() - rowStart);
	}

	table.rowStarts.push_back(table.cells.size());
	return table;
}

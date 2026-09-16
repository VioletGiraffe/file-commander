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

QChar detectCsvDelimiter(QStringView text)
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
	for (qsizetype lineStart = 0; lineStart < text.size() && lines < sampleLines;)
	{
		qsizetype lineEnd = text.indexOf(u'\n', lineStart);
		if (lineEnd < 0)
			lineEnd = text.size();

		const QStringView line = text.mid(lineStart, lineEnd - lineStart);
		lineStart = lineEnd + 1;
		if (line.trimmed().isEmpty())
			continue;

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
	}

	const auto rank = [&stats](size_t index) { return std::pair{ stats[index].total > 0 && stats[index].consistent, stats[index].total }; };
	size_t best = 0;
	for (size_t i = 1; i < candidates.size(); ++i)
	{
		if (rank(i) > rank(best))
			best = i;
	}

	return stats[best].total > 0 ? QChar{ candidates[best] } : QChar{ u',' };
}

CsvTable parseCsv(QString text, QChar delimiter)
{
	constexpr QChar quote = u'"', cr = u'\r', lf = u'\n';

	CsvTable table;
	table.text = std::move(text);
	QChar* const begin = table.text.data();
	QChar* const end = begin + table.text.size();

	QChar* rp = begin; // read cursor
	while (rp < end)
	{
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

		// CRLF, LF, lone CR, or the end of input
		if (rp < end && *rp == cr)
			++rp;
		if (rp < end && *rp == lf)
			++rp;

		// A blank line parses as one empty cell
		if (table.cells.size() == rowStart + 1 && table.cells.back().length == 0)
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

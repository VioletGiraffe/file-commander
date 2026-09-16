#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

// A cell is a slice of CsvTable::text
struct CsvCell
{
	qsizetype offset;
	qsizetype length;
};

struct CsvTable
{
	QString text;
	// Row i spans cells[rowStarts[i], rowStarts[i + 1]); one extra entry closes the last row
	std::vector<size_t> rowStarts;
	std::vector<CsvCell> cells;
	size_t columnCount = 0; // The longest row; shorter rows simply lack the trailing cells, comment rows do not count
	// A comment row holds its whole line, commentPrefix included, as its only cell
	std::vector<bool> isCommentRow;
	size_t commentRowCount = 0;

	[[nodiscard]] size_t rowCount() const noexcept { return rowStarts.empty() ? 0 : rowStarts.size() - 1; }
	// Empty for a column the row does not reach
	[[nodiscard]] QStringView cell(size_t row, size_t column) const noexcept;
};

// Not RFC 4180: a common extension for metadata lines
inline constexpr QChar commentPrefix = u'#';

// File-wide: lines starting with commentPrefix are comments if no other line contains it, and any of these holds:
//   - they are a leading block
//   - they are at most a tenth as many as the other lines
//   - most of them have a delimiter count other than the one most common among the other lines
[[nodiscard]] bool csvHasCommentLines(QStringView text);

// The candidate with the same count on every one of the first lines wins; among several, the most frequent. Comma if none appears at all.
[[nodiscard]] QChar detectCsvDelimiter(QStringView text, bool skipCommentLines);

// RFC 4180: quoted fields may hold the delimiter, line breaks and "" for a quote. Both CRLF and LF end a row; blank lines are skipped.
// Quoted fields are unescaped in place: the content only shrinks, so it is rewritten over its own characters.
// recognizeCommentLines: a row starting with commentPrefix is a comment row up to its line break, quotes in it ignored.
[[nodiscard]] CsvTable parseCsv(QString text, QChar delimiter, bool recognizeCommentLines);

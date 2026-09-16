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
	size_t columnCount = 0; // The longest row; shorter rows simply lack the trailing cells

	[[nodiscard]] size_t rowCount() const noexcept { return rowStarts.empty() ? 0 : rowStarts.size() - 1; }
	// Empty for a column the row does not reach
	[[nodiscard]] QStringView cell(size_t row, size_t column) const noexcept;
};

// The candidate with the same count on every one of the first lines wins; among several, the most frequent. Comma if none appears at all.
[[nodiscard]] QChar detectCsvDelimiter(QStringView text);

// RFC 4180: quoted fields may hold the delimiter, line breaks and "" for a quote. Both CRLF and LF end a row; blank lines are skipped.
// Quoted fields are unescaped in place: the content only shrinks, so it is rewritten over its own characters.
[[nodiscard]] CsvTable parseCsv(QString text, QChar delimiter);

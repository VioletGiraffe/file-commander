#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <Qt>
RESTORE_COMPILER_WARNINGS

#include <vector>

// A filter field holds the filters separated by ';'
[[nodiscard]] QStringList splitNameFilterField(const QString& fieldText);

// A name matches if any of the filters does.
// The name filter language: literal text, '*' for any run of characters, '?' for exactly one. A leading '^' or a
// trailing '$' anchors that end of the match to the name's; an end left unanchored matches anywhere.
class FileNameFilter
{
public:
	FileNameFilter() = default; // Matches every name
	// With no filters, or with "*" among them, matches every name
	FileNameFilter(const QStringList& filters, Qt::CaseSensitivity caseSensitivity);

	[[nodiscard]] bool matches(QStringView name) const;

private:
	std::vector<QRegularExpression> _filters; // Empty when it matches every name
};

#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

class CPanel;

struct FileSearchQuery
{
	QStringList roots; // Absolute
	QStringList nameFilters; // See FileNameFilter
	bool nameCaseSensitive = false;
	QString contents; // Empty leaves the contents unexamined
	bool contentsCaseSensitive = false;
	bool contentsWholeWords = false;
	bool contentsIsRegex = false;
};

// The filters in the text of a search dialog's name field: see splitNameFilterField().
// Without partial match a filter matches the whole name, and a '^' or '$' typed in it is a literal character.
[[nodiscard]] QStringList nameFiltersFromSearchField(const QString& fieldText, bool partialMatch);

// The paths in the text of a search dialog's "Search in" field, separated by "; ".
// Each is typed path text, resolved against baseDir: see resolveUserPath().
[[nodiscard]] QStringList rootsFromSearchInField(const QString& fieldText, const QString& baseDir);

// A "Search in" field's text for the panel: its selected items, or with nothing selected its folder
[[nodiscard]] QString searchInFieldTextFor(const CPanel& panel);

#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QRegularExpression>
#include <QString>
RESTORE_COMPILER_WARNINGS

// Matches full names against a Qt wildcard, unanchored and case-insensitive
class FileNameFilter
{
public:
	// Empty lets every row through
	void setWildcard(const QString& wildcard)
	{
		_wildcard = wildcard;
		_regex.setPattern(wildcard.isEmpty() ? QString{} : QRegularExpression::wildcardToRegularExpression(wildcard, QRegularExpression::UnanchoredWildcardConversion));
	}

	[[nodiscard]] const QString& wildcard() const noexcept
	{
		return _wildcard;
	}

	[[nodiscard]] bool matches(const CFileSystemObject& row) const
	{
		return _wildcard.isEmpty() || row.fullName().contains(_regex);
	}

private:
	QString _wildcard;
	QRegularExpression _regex{ QString{}, QRegularExpression::CaseInsensitiveOption };
};

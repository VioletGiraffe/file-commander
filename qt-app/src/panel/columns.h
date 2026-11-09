#pragma once

#include "filelist/filelistsorter.h"

// All the columns of a panel
enum FileListViewColumn {NameColumn, ExtColumn, SizeColumn, DateColumn, NumberOfColumns};

// Each column sorts by the field of the same value
static_assert((int)FileListSortField::Name == NameColumn && (int)FileListSortField::Extension == ExtColumn);
static_assert((int)FileListSortField::Size == SizeColumn && (int)FileListSortField::Date == DateColumn);

[[nodiscard]] constexpr int columnOf(FileListSortField field) noexcept
{
	return (int)field;
}

// Requires a column in [0, NumberOfColumns)
[[nodiscard]] constexpr FileListSortField sortFieldOf(int column) noexcept
{
	return (FileListSortField)column;
}

#include "filelistsorter.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"


#include <utility>

bool isFileOrBundle(const CFileSystemObject& row) noexcept
{
	return row.isFile() || row.isBundle();
}

QStringView fileListDisplayName(const CFileSystemObject& row) noexcept
{
	const QStringView name = row.name();
	return name.isEmpty() && isFileOrBundle(row) ? row.fullName() : name;
}

template <typename T>
[[nodiscard]] static int compareValues(const T& l, const T& r) noexcept
{
	return l < r ? -1 : (r < l ? 1 : 0);
}

void FileListSorter::setSorting(FileListSortField field, Qt::SortOrder order) noexcept
{
	_sortField = field;
	_sortOrder = order;
}

FileListSortField FileListSorter::sortField() const noexcept
{
	return _sortField;
}

Qt::SortOrder FileListSorter::sortOrder() const noexcept
{
	return _sortOrder;
}

FileListSortKeys FileListSorter::sortKeysFor(const CFileSystemObject& row)
{
	QCollatorSortKey nameKey = NaturalSort::sortKey(fileListDisplayName(row).toString());

	const QStringView extension = row.extension();
	if (const auto cached = _sortKeyByExtension.find(extension); cached != _sortKeyByExtension.end())
		return { std::move(nameKey), cached->second };

	QString extensionString = extension.toString();
	QCollatorSortKey extensionKey = NaturalSort::sortKey(extensionString);
	return { std::move(nameKey), _sortKeyByExtension.emplace(std::move(extensionString), std::move(extensionKey)).first->second };
}

void FileListSorter::clearExtensionKeyCache()
{
	_sortKeyByExtension.clear();
}

bool FileListSorter::lessThan(const CFileSystemObject& l, const FileListSortKeys& lKeys, const CFileSystemObject& r, const FileListSortKeys& rKeys) const
{
	if (l.isCdUp() != r.isCdUp())
		return l.isCdUp();
	if (isFileOrBundle(l) != isFileOrBundle(r))
		return isFileOrBundle(r);

	int result = compareBySortField(l, lKeys, r, rKeys);
	if (result == 0)
		result = NaturalSort::compare(lKeys.name, rKeys.name);
	if (result == 0)
		result = NaturalSort::compare(lKeys.extension, rKeys.extension);
	// Not the collator for the path: it can call two different strings equal
	if (result == 0)
		result = l.fullAbsolutePath().compare(r.fullAbsolutePath());

	return _sortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
}

int FileListSorter::compareBySortField(const CFileSystemObject& l, const FileListSortKeys& lKeys, const CFileSystemObject& r, const FileListSortKeys& rKeys) const
{
	switch (_sortField)
	{
	case FileListSortField::Name:
		return NaturalSort::compare(lKeys.name, rKeys.name);
	case FileListSortField::Extension:
		// Folders by name, files by extension, then name
		if (isFileOrBundle(l))
		{
			if (const int byExtension = NaturalSort::compare(lKeys.extension, rKeys.extension); byExtension != 0)
				return byExtension;
		}
		return NaturalSort::compare(lKeys.name, rKeys.name);
	case FileListSortField::Size:
		return compareValues(l.size(), r.size());
	case FileListSortField::Date:
		return compareValues(l.modificationTime(), r.modificationTime());
	}

	assert_unconditional_r("Unhandled sort field");
	return 0;
}

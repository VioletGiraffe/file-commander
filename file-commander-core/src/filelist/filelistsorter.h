#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "qtcore_helpers/qstring_hash.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QCollatorSortKey>
#include <QString>
#include <QStringView>
#include <Qt>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

[[nodiscard]] bool isFileOrBundle(const CFileSystemObject& row) noexcept;

// A file with nothing before its extension, like .jpg on Windows, displays and sorts by its full name
[[nodiscard]] QStringView fileListDisplayName(const CFileSystemObject& row) noexcept;
// What the name column shows before the name: the row's folder relative to listedFolder, native, ending with a separator.
// Empty for [..], for a child of listedFolder, and when listedFolder is empty; the whole folder path for a row outside it.
// listedFolder: the panel's folder, ending with '/'. A view of items from many folders lists them all in it.
[[nodiscard]] QString fileListFolderPrefix(const CFileSystemObject& row, const QString& listedFolder);

enum class FileListSortField { Name, Extension, Size, Date };

struct FileListSortKeys {
	QCollatorSortKey name; // Of fileListFolderPrefix() and fileListDisplayName()
	QCollatorSortKey extension;
};

// Orders file list rows: [..] first, then folders, then files, in either direction.
// Within a group, rows sort by the sort field, then by name, extension and exact path: a total order.
class FileListSorter
{
public:
	void setSorting(FileListSortField field, Qt::SortOrder order) noexcept;
	[[nodiscard]] FileListSortField sortField() const noexcept;
	[[nodiscard]] Qt::SortOrder sortOrder() const noexcept;
	// See fileListFolderPrefix(). The keys from sortKeysFor() depend on it.
	void setListedFolder(QString folder);
	[[nodiscard]] const QString& listedFolder() const noexcept;

	// Rows with the same extension share one key, cached until clearExtensionKeyCache()
	[[nodiscard]] FileListSortKeys sortKeysFor(const CFileSystemObject& row);
	void clearExtensionKeyCache();

	// Requires each row's keys from sortKeysFor()
	[[nodiscard]] bool lessThan(const CFileSystemObject& l, const FileListSortKeys& lKeys, const CFileSystemObject& r, const FileListSortKeys& rKeys) const;
	void sort(std::vector<CFileSystemObject>& rows);

private:
	// Ascending order by the sort field alone
	[[nodiscard]] int compareBySortField(const CFileSystemObject& l, const FileListSortKeys& lKeys, const CFileSystemObject& r, const FileListSortKeys& rKeys) const;

	ankerl::unordered_dense::map<QString, QCollatorSortKey, QStringHash, std::equal_to<>> _sortKeyByExtension;
	QString _listedFolder;
	FileListSortField _sortField = FileListSortField::Name;
	Qt::SortOrder _sortOrder = Qt::AscendingOrder;
};

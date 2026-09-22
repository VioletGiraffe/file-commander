#pragma once

#include "panel/columns.h"

#include "cfilesystemobject.h"
#include "detail/hashmap_helpers.h"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QAbstractItemModel>
#include <QList>
#include <QRegularExpression>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <stdint.h>
#include <time.h>
#include <vector>

enum Role {
	FullNameRole = Qt::UserRole+1
};

// One entry of the list: the fields it displays and sorts by, copied from its CFileSystemObject
struct FileListRow
{
	[[nodiscard]] static FileListRow fromObject(const CFileSystemObject& object);

	[[nodiscard]] bool isFileOrBundle() const noexcept { return type == File || type == Bundle; }
	[[nodiscard]] bool isDir() const noexcept { return type == Directory || type == Bundle; }

	// A file with nothing before its extension, like .bashrc, displays and sorts as its full name with no extension
	[[nodiscard]] const QString& displayName() const noexcept;
	[[nodiscard]] const QString& displayExtension() const noexcept;

	QString fullPath;
	QString fullName; // Name and extension
	QString name;
	QString extension;
	qulonglong hash = 0;
	uint64_t size = 0;
	time_t modificationTime = 0;
	FileSystemObjectType type = UnknownType;
	bool isCdUp = false;
};

struct FolderContentsSummary {
	uint64_t numFiles = 0;
	uint64_t numFolders = 0;
	uint64_t size = 0; // A folder counts only once its size has been calculated
};

class CIconProvider;

// The file list of one tab: owns its rows, and sorts and filters them
class CFileListModel final : public QAbstractItemModel
{
	Q_OBJECT
public:
	// Handles dropped URLs; an empty destinationPath means the current folder
	using DropHandler = std::function<bool (const QList<QUrl>& urls, Qt::DropAction action, const QString& destinationPath)>;

	// iconProvider: null leaves the rows without icons
	explicit CFileListModel(CIconProvider* iconProvider, QObject* parent = nullptr);

	void setDropHandler(DropHandler handler);

	// Resets the model
	void setRows(std::vector<FileListRow> rows);
	// Hides the rows whose full name does not match; the wildcard is unanchored and case-insensitive. Empty shows every row.
	void setNameFilter(const QString& wildcard);

	void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
	[[nodiscard]] int sortColumn() const noexcept;
	[[nodiscard]] Qt::SortOrder sortOrder() const noexcept;

	[[nodiscard]] const FileListRow& rowAt(int row) const;
	[[nodiscard]] const FileListRow& rowAt(const QModelIndex& index) const;
	[[nodiscard]] qulonglong itemHash(const QModelIndex& index) const; // 0 for an invalid index
	[[nodiscard]] QModelIndex indexByHash(qulonglong hash) const; // Invalid if no row has it, or the filter hides it
	// The topmost row holding a file (folders always sort above files), or -1 if there are no files
	[[nodiscard]] int firstFileRow() const;
	// Totals over every row, the hidden ones included
	[[nodiscard]] const FolderContentsSummary& contentsSummary() const noexcept;

	// Repaints the rows whose precise icon has just been retrieved. Hashes of rows not shown are ignored.
	void onPreciseIconsAvailable(const std::vector<qulonglong>& objectHashes);
	// Repaints every icon after the provider dropped its cache.
	void onAllIconsInvalidated();

	[[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
	[[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;

	[[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
	[[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role) override;
	[[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

// Drag and drop
	[[nodiscard]] bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
	[[nodiscard]] QStringList mimeTypes() const override;
	bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;
	[[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;

signals:
	void itemEdited(qulonglong itemHash, QString newName);
	// Emitted by every sort() call, including one that leaves the order as it was
	void sorted();

private:
	[[nodiscard]] bool rowLessThan(const FileListRow& l, const FileListRow& r) const;
	[[nodiscard]] std::vector<uint32_t> displayedRowsInOrder() const;
	void updateRowByHash();
	// Re-filters and re-sorts; every persistent index (selection, cursor, editor) follows its row or is dropped with it
	void relayout(QAbstractItemModel::LayoutChangeHint hint);

private:
	std::vector<FileListRow> _rows;
	// Indices into _rows: the rows the filter lets through, in display order
	std::vector<uint32_t> _displayedRows;
	ankerl::unordered_dense::map<qulonglong, int, IdentityHash> _displayRowByHash;
	FolderContentsSummary _contentsSummary;
	QRegularExpression _nameFilter;
	DropHandler _dropHandler;
	CIconProvider* const _iconProvider;
	int _sortColumn = NameColumn;
	Qt::SortOrder _sortOrder = Qt::AscendingOrder;
};

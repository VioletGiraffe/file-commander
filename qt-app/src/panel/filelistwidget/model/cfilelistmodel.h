#pragma once

#include "panel/columns.h"

#include "cfilesystemobject.h"
#include "filelist/filelistsorter.h"
#include "filelist/filenamefilter.h"
#include "filelist/foldercontentssummary.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "hash/hash_functors.hpp"


DISABLE_COMPILER_WARNINGS
#include <3rdparty/ankerl/unordered_dense.h>

#include <QAbstractItemModel>
#include <QList>
#include <QString>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <stdint.h>
#include <vector>

enum Role {
	FullNameRole = Qt::UserRole+1,
	FileGlyphRole // The FileGlyph, as an int, when the icon is one; a delegate draws it in the row's text colour
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

	// Resets the model. listedFolder: see fileListFolderPrefix().
	void setRows(std::vector<CFileSystemObject> rows, QString listedFolder = {});
	// Removes, inserts, moves and changes single rows until the rows match 'rows', so every persistent index (selection, cursor, an open editor)
	// stays on its row. Returns false if it reset the model instead: past a number of changes, one reset is cheaper.
	// The rows are in the folder setRows() took.
	bool updateRows(std::vector<CFileSystemObject> rows);
	// filterText: a filter field's text (see splitNameFilterField()), matched against full names ignoring case. Empty shows every row.
	void setNameFilter(const QString& filterText);

	void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
	[[nodiscard]] int sortColumn() const noexcept;
	[[nodiscard]] Qt::SortOrder sortOrder() const noexcept;

	[[nodiscard]] const CFileSystemObject& rowAt(int row) const;
	[[nodiscard]] const CFileSystemObject& rowAt(const QModelIndex& index) const;
	[[nodiscard]] qulonglong itemHash(const QModelIndex& index) const; // 0 for an invalid index
	[[nodiscard]] QModelIndex indexByHash(qulonglong hash) const; // Invalid if no row has it, or the filter hides it
	// The topmost row holding a file (folders always sort above files), or -1 if there are no files
	[[nodiscard]] int firstFileRow() const;
	// Totals over every row, the hidden ones included
	[[nodiscard]] const FolderContentsSummary& contentsSummary() const noexcept;

	// Repaints the rows whose icon changed. Hashes of rows not shown are ignored.
	void onIconsChanged(const std::vector<qulonglong>& objectHashes);
	// Repaints every icon after the provider dropped its cache.
	void onAllIconsChanged();

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
	// Arguments are indices into _rows
	[[nodiscard]] bool rowLessThan(uint32_t l, uint32_t r) const;
	[[nodiscard]] std::vector<uint32_t> displayedRowsInOrder() const;
	// Where _rows[rowIndex] sorts among the displayed rows other than 'displayRow', as the destination of moving displayRow
	[[nodiscard]] int moveDestination(uint32_t rowIndex, int displayRow) const;
	void rebuildDisplayRowByHash() const;
	void updateContentsSummary();
	// Re-filters and re-sorts; every persistent index (selection, cursor, editor) follows its row or is dropped with it
	void relayout(QAbstractItemModel::LayoutChangeHint hint);

private:
	std::vector<CFileSystemObject> _rows;
	std::vector<FileListSortKeys> _sortKeys; // Parallel to _rows
	FileListSorter _sorter; // Caches the key of every extension seen since the last setRows()
	QString _nameFilterText;
	FileNameFilter _nameFilter;
	// Indices into _rows: the rows the filter lets through, in display order
	std::vector<uint32_t> _displayedRows;
	// Rebuilt by the first lookup after _displayedRows changes: updateRows() changes it once per single-row change
	mutable ankerl::unordered_dense::map<qulonglong, int, identity_hash> _displayRowByHash;
	mutable bool _displayRowByHashIsStale = false;
	FolderContentsSummary _contentsSummary;
	DropHandler _dropHandler;
	CIconProvider* const _iconProvider;
};

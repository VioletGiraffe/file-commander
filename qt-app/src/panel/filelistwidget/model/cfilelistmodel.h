#pragma once

#include "panel/columns.h"

#include "cfilesystemobject.h"
#include "filelist/filetimetext.h"
#include "filelist/foldercontentssummary.h"
#include "filelist/sortedfilelist.h"


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
#include <vector>

enum Role {
	FullNameRole = Qt::UserRole+1,
	FileGlyphRole // The FileGlyph, as an int, when the icon is one; a delegate draws it in the row's text colour
};

class CIconProvider;
class QTreeView;

// How a file list shows its Date and Size columns
struct FileListCellFormat
{
	FileTimeTextLength dateLength = FileTimeTextLength::Short;
	// Compact: the panels' "1.5 K". Precise: fileSizeToString()'s four significant digits, for files ranked by size.
	enum class SizeText { Compact, Precise } sizeText = SizeText::Compact;
};

// The file list of one tab: a SortedFileList as a Qt model
class CFileListModel final : public QAbstractItemModel, private SortedFileListObserver
{
	Q_OBJECT
public:
	// Handles dropped URLs; an empty destinationPath means the current folder
	using DropHandler = std::function<bool (const QList<QUrl>& urls, Qt::DropAction action, const QString& destinationPath)>;

	// iconProvider: null leaves the rows without icons
	explicit CFileListModel(CIconProvider* iconProvider, FileListCellFormat cellFormat = {}, QObject* parent = nullptr);

	void setDropHandler(DropHandler handler);

	// Below, a member with a namesake in SortedFileList has that namesake's contract

	// Resets the model, and clears the marks of markReachedThroughLink()
	void setRows(std::vector<CFileSystemObject> rows, QString listedFolder);
	// Every persistent index (selection, cursor, an open editor) stays on its row. Returns false if it reset the model instead.
	bool updateRows(std::vector<CFileSystemObject> rows);
	void addRows(std::vector<CFileSystemObject> rows);
	void setNameFilter(const QString& filterText);

	// The item's row is in italics, with a tooltip: a search found the item by following a directory link
	void markReachedThroughLink(qulonglong itemHash);

	void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;
	[[nodiscard]] int sortColumn() const noexcept;
	[[nodiscard]] Qt::SortOrder sortOrder() const noexcept;

	[[nodiscard]] const CFileSystemObject& rowAt(int row) const;
	[[nodiscard]] const CFileSystemObject& rowAt(const QModelIndex& index) const;
	[[nodiscard]] qulonglong itemHash(const QModelIndex& index) const; // 0 for an invalid index
	[[nodiscard]] QModelIndex indexByHash(qulonglong hash) const; // Invalid if no row has it, or the filter hides it
	[[nodiscard]] int firstFileRow() const;
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
	void beforeReset() override;
	void afterReset() override;
	void beforeInsert(int first, int last) override;
	void afterInsert() override;
	void beforeRemove(int first, int last) override;
	void afterRemove() override;
	void beforeMove(int row, int destination) override;
	void afterMove() override;
	void rowChanged(int row) override;
	// Every persistent index (selection, cursor, editor) follows its row or is dropped with it
	void beforeReorder(SortedFileListReorderCause cause) override;
	void afterReorder(SortedFileListReorderCause cause) override;

private:
	SortedFileList _list{ *this };
	// From beforeReorder() to afterReorder(): the persistent indexes and the hashes of their rows
	QModelIndexList _indexesBeforeReorder;
	std::vector<qulonglong> _hashesBeforeReorder;
	ankerl::unordered_dense::set<qulonglong, identity_hash> _hashesReachedThroughLink;
	DropHandler _dropHandler;
	CIconProvider* const _iconProvider;
	const FileListCellFormat _cellFormat;
};

// Shows the model in a plain tree view, outside the panels: Name takes the width the other columns leave, each fitted to the rows on screen.
// No row can be renamed there.
void showFileListInTreeView(CFileListModel& model, QTreeView& view, int sortColumn, Qt::SortOrder sortOrder);

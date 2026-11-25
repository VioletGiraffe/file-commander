#include "ctuifilelist.h"
#include "ctuicolourtheme.h"
#include "ctuipalette.h"

#include "filelist/filesizetext.h"
#include "filelist/filetimetext.h"
#include "settings.h"

#define Uses_TDrawBuffer
#define Uses_TEvent
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <array>
#include <optional>

namespace {

constexpr int minNameColumnWidth = 20;

// Zero for Name, whose width is what the others leave
[[nodiscard]] int fixedColumnWidth(FileListSortField field, FileTimeTextLength dateTextLength) noexcept
{
	switch (field)
	{
	case FileListSortField::Name: return 0;
	case FileListSortField::Extension: return 5; // "Ext ↑"
	case FileListSortField::Size: return 6; // "Size ↑"
	case FileListSortField::Date: return dateTextLength == FileTimeTextLength::Long ? longFileTimeTextLength : maxShortFileTimeTextLength;
	}
	return 0;
}

[[nodiscard]] QString columnTitle(FileListSortField field)
{
	switch (field)
	{
	case FileListSortField::Name: return QStringLiteral("Name");
	case FileListSortField::Extension: return QStringLiteral("Ext");
	case FileListSortField::Size: return QStringLiteral("Size");
	case FileListSortField::Date: return QStringLiteral("Date");
	}
	return {};
}

// The GUI's setting, read at the first draw: a change applies from the next start
[[nodiscard]] bool columnSeparatorsShown()
{
	static const bool shown = QSettings{}.value(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, INTERFACE_SHOW_COLUMN_SEPARATORS_DEFAULT).toBool();
	return shown;
}

} // namespace

std::vector<FileListColumn> fileListColumns(const int width, const FileListSortField sortField, const FileTimeTextLength dateTextLength)
{
	// One cell before each column, and one after the last
	int nameWidth = width - 2;
	std::array<bool, 4> shown{};
	shown[(size_t)FileListSortField::Name] = true;
	const auto show = [&](FileListSortField field) {
		shown[(size_t)field] = true;
		nameWidth -= fixedColumnWidth(field, dateTextLength) + 1;
	};

	if (sortField != FileListSortField::Name)
		show(sortField);
	for (const FileListSortField field : { FileListSortField::Size, FileListSortField::Extension, FileListSortField::Date })
	{
		if (!shown[(size_t)field] && nameWidth - fixedColumnWidth(field, dateTextLength) - 1 >= minNameColumnWidth)
			show(field);
	}

	std::vector<FileListColumn> columns;
	int x = 1;
	for (const FileListSortField field : { FileListSortField::Name, FileListSortField::Extension, FileListSortField::Size, FileListSortField::Date })
	{
		if (!shown[(size_t)field])
			continue;

		const int columnWidth = field == FileListSortField::Name ? std::max(nameWidth, 0) : fixedColumnWidth(field, dateTextLength);
		columns.push_back({ field, x, columnWidth });
		x += columnWidth + 1;
	}
	return columns;
}

QString fileListCellText(const CFileSystemObject& item, FileListSortField field, bool extensionShown, const QString& listedFolder, FileTimeTextLength dateTextLength)
{
	switch (field)
	{
	case FileListSortField::Name: return fileListFolderPrefix(item, listedFolder) % (extensionShown ? fileListDisplayName(item) : item.fullName());
	case FileListSortField::Extension: return item.extension().toString();
	case FileListSortField::Size:
		// A folder has a size once Space has calculated it
		if (item.isDir() && item.size() == 0)
			return item.isCdUp() ? QStringLiteral("<UP>") : QStringLiteral("<DIR>");
		return compactFileSizeText(item.size(), FileSizeUnitSpacing::Joined);
	case FileListSortField::Date:
		return item.isCdUp() ? QString{} : fileTimeText(item.modificationTime(), dateTextLength);
	}
	return {};
}

void drawFileListCell(TDrawBuffer& line, const FileListColumn& column, const QString& text, TColorAttr color)
{
	const QByteArray utf8 = text.toUtf8();
	const TStringView view{ utf8.constData(), (size_t)utf8.size() };
	const int indent = column.field == FileListSortField::Size ? column.x + std::max(column.width - strwidth(view), 0) : column.x;
	line.moveStr((ushort)indent, view, color, (ushort)column.width);
}

void drawFileListColumnSeparators(TDrawBuffer& line, const std::vector<FileListColumn>& columns, TColorAttr color)
{
	if (!columnSeparatorsShown())
		return;

	for (size_t i = 1; i < columns.size(); ++i)
		line.moveStr((ushort)(columns[i].x - 1), "┆", color);
}

CTuiColumnHeader::CTuiColumnHeader(const TRect& bounds, const Look& look) noexcept : TView(bounds), _look(look)
{
	growMode = gfGrowHiX;
}

void CTuiColumnHeader::setSorting(FileListSortField field, Qt::SortOrder order)
{
	_sortField = field;
	_sortOrder = order;
	drawView();
}

void CTuiColumnHeader::clearSorting()
{
	_sortField = FileListSortField::Name;
	_sortOrder.reset();
	drawView();
}

void CTuiColumnHeader::draw()
{
	const TColorAttr color = mapColor(_look.textColourEntry);
	TDrawBuffer line;
	line.moveChar(0, ' ', color, (ushort)size.x);
	const std::vector<FileListColumn> columns = fileListColumns(size.x, _sortField, _look.dateTextLength);
	for (const FileListColumn& column : columns)
	{
		QString title = columnTitle(column.field);
		if (appliedColourTheme().parameters.uppercaseColumnHeader)
			title = title.toUpper();
		if (_sortOrder && column.field == _sortField)
			title += *_sortOrder == Qt::AscendingOrder ? QStringLiteral(" ↑") : QStringLiteral(" ↓");
		drawFileListCell(line, column, title, color);
	}
	drawFileListColumnSeparators(line, columns, mapColor(_look.separatorColourEntry));
	writeLine(0, 0, (short)size.x, 1, line);
}

void CTuiColumnHeader::handleEvent(TEvent& event)
{
	TView::handleEvent(event);
	if (event.what != evMouseDown)
		return;

	const int x = makeLocal(event.mouse.where).x;
	const std::vector<FileListColumn> columns = fileListColumns(size.x, _sortField, _look.dateTextLength);
	const auto clicked = std::ranges::find_if(columns, [x](const FileListColumn& column) { return x >= column.x && x < column.x + column.width; });
	if (clicked != columns.end() && (event.mouse.buttons & mbLeftButton))
		onColumnClicked(clicked->field);
	clearEvent(event);
}

CTuiFileList::CTuiFileList(const TRect& bounds, TScrollBar* verticalScrollBar) noexcept :
	CTuiRowList(bounds, verticalScrollBar)
{
}

void CTuiFileList::setContents(Contents contents, const qulonglong cursorItemHash, const int fallbackRow)
{
	const CFileSystemObject* previousCursorItem = currentRow();
	const bool cursorStaysOnItem = previousCursorItem && previousCursorItem->hash() == cursorItemHash;

	// Emptied first: the filter and the sorting below would re-sort rows about to be replaced
	if (!contents.refreshesShownView)
		_list.setRows({}, {});
	_list.setNameFilter(contents.nameFilter);
	_list.setSorting(contents.sortField, contents.sortOrder);
	if (contents.refreshesShownView)
		_list.updateRows(std::move(contents.items));
	else
		_list.setRows(std::move(contents.items), std::move(contents.listedFolder));

	finishRowsChange(contents.selectedItemHashes, cursorItemHash, cursorStaysOnItem, fallbackRow);
}

void CTuiFileList::applyChanges(std::vector<CFileSystemObject> upserted, const std::vector<qulonglong>& removedHashes,
	const std::vector<qulonglong>& selectedItemHashes, const qulonglong cursorItemHash)
{
	const CFileSystemObject* previousCursorItem = currentRow();
	const bool cursorStaysOnItem = previousCursorItem && previousCursorItem->hash() == cursorItemHash;
	_list.applyChanges(std::move(upserted), removedHashes);
	finishRowsChange(selectedItemHashes, cursorItemHash, cursorStaysOnItem, cursorRow());
}

void CTuiFileList::finishRowsChange(const std::vector<qulonglong>& selectedItemHashes, qulonglong cursorItemHash, bool cursorStaysOnItem, int fallbackRow)
{
	_selectedItemHashes.clear();
	_selectedItemHashes.insert(selectedItemHashes.begin(), selectedItemHashes.end());
	_provisionalDirSizes.clear();

	const int cursorItemRow = _list.rowOf(cursorItemHash);
	if (cursorStaysOnItem && cursorItemRow >= 0)
		rowsChangedKeepingCursorItem(cursorItemRow);
	else
		rowsChanged(cursorItemRow >= 0 ? cursorItemRow : fallbackRow);
}

void CTuiFileList::clearRowsKeepingScreen()
{
	_list.setRows({}, {});
	_selectedItemHashes.clear();
	_provisionalDirSizes.clear();
	rowsEmptiedWithoutRedraw();
}

void CTuiFileList::showEmptyRows()
{
	rowsChanged(0);
}

const CFileSystemObject* CTuiFileList::currentRow() const noexcept
{
	return _list.rowCount() > 0 ? &_list.rowAt(cursorRow()) : nullptr;
}

std::optional<int> CTuiFileList::firstFileRow() const
{
	const int row = _list.firstFileRow();
	return row >= 0 ? std::optional{ row } : std::nullopt;
}

void CTuiFileList::setSelectedItems(const std::vector<qulonglong>& itemHashes)
{
	_selectedItemHashes.clear();
	_selectedItemHashes.insert(itemHashes.begin(), itemHashes.end());
	drawView();
}

bool CTuiFileList::isSelected(qulonglong itemHash) const noexcept
{
	return _selectedItemHashes.contains(itemHash);
}

void CTuiFileList::setProvisionalDirSizes(const std::vector<std::pair<qulonglong, uint64_t>>& sizes)
{
	bool changed = false;
	for (const auto& [hash, dirSize] : sizes)
	{
		auto [shown, inserted] = _provisionalDirSizes.try_emplace(hash, dirSize);
		changed = changed || inserted || shown->second != dirSize;
		shown->second = dirSize;
	}
	if (changed)
		drawView();
}

void CTuiFileList::draw()
{
	struct OffCursorRowColors { TColorAttr normal, normalDetails, selected, separator; };
	const OffCursorRowColors plainRowColors{ mapColor(ppFileListRow), mapColor(ppFileListRowDetails), mapColor(ppFileListSelectedRow), mapColor(ppColumnSeparator) },
		bandRowColors{ mapColor(ppFileListBandRow), mapColor(ppFileListBandRowDetails), mapColor(ppFileListBandSelectedRow), mapColor(ppBandColumnSeparator) };
	const TColorAttr cursorColor = mapColor(ppFileListCursor), selectedCursorColor = mapColor(ppFileListSelectedCursor), cursorSeparatorColor = mapColor(ppCursorColumnSeparator);
	const int rowsPerBand = appliedColourTheme().parameters.rowsPerBand;

	const std::vector<FileListColumn> columns = fileListColumns(size.x, _list.sortField(), FileTimeTextLength::Short);
	const bool extensionShown = std::ranges::any_of(columns, [](const FileListColumn& column) { return column.field == FileListSortField::Extension; });

	for (int y = 0; y < size.y; ++y)
	{
		const int row = topRow() + y;
		const CFileSystemObject* item = row < _list.rowCount() ? &_list.rowAt(row) : nullptr;
		const bool selected = item && isSelected(item->hash());
		const bool onCursor = cursorIsShownOn(row);
		// The bands are counted from the list's first row, and continue below its last
		const OffCursorRowColors& offCursor = (row / rowsPerBand) % 2 == 0 ? plainRowColors : bandRowColors;
		const TColorAttr color = onCursor ? (selected ? selectedCursorColor : cursorColor) : (selected ? offCursor.selected : offCursor.normal);
		const TColorAttr detailsColor = onCursor || selected ? color : offCursor.normalDetails;

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		if (item)
		{
			for (const FileListColumn& column : columns)
			{
				const auto provisionalSize = column.field == FileListSortField::Size ? _provisionalDirSizes.find(item->hash()) : _provisionalDirSizes.end();
				if (provisionalSize != _provisionalDirSizes.end())
				{
					drawFileListCell(line, column, compactFileSizeText(provisionalSize->second, FileSizeUnitSpacing::Joined), onCursor ? cursorSeparatorColor : offCursor.separator);
					continue;
				}

				const TColorAttr columnColor = column.field == FileListSortField::Name ? color : detailsColor;
				drawFileListCell(line, column, fileListCellText(*item, column.field, extensionShown, _list.listedFolder(), FileTimeTextLength::Short), columnColor);
			}
		}
		drawFileListColumnSeparators(line, columns, onCursor ? cursorSeparatorColor : offCursor.separator);

		writeLine(0, (short)y, (short)size.x, 1, line);
	}
}

int CTuiFileList::rowCount() const noexcept
{
	return _list.rowCount();
}

void CTuiFileList::cursorMovedByUser()
{
	if (onCursorMoved)
		onCursorMoved(_list.rowAt(cursorRow()));
}

#include "ctuipanel.h"
#include "ctuiapplication.h"
#include "ctuibatchrenamedialog.h"
#include "ctuicolourtheme.h"
#include "ctuidialogs.h"
#include "ctuifavoritesdialog.h"
#include "ctuifilelist.h"
#include "ctuifileoperations.h"
#include "ctuipalette.h"
#include "ctuiprograms.h"
#include "ctuiviewer.h"

#include "ccontroller.h"
#include "filecomparator/cfilecomparator.h"
#include "filelist/filetimetext.h"
#include "filesystemhelperfunctions.h"
#include "filesystemhelpers/filestatistics.h"

#include "fileoperations/fileoperationtexts.h"

#include "assert/advanced_assert.h"

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TFrame
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TScrollBar
#define Uses_TView
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

// Edits the panel's name filter
class CTuiFilterLine final : public TInputLine
{
public:
	explicit CTuiFilterLine(const TRect& bounds) : TInputLine(bounds, 256)
	{
		growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
	}

	std::function<void(const QString& text)> onEdited;
	std::function<void()> onConfirmed; // Enter
	std::function<void()> onCancelled; // Esc

	[[nodiscard]] QString text() const { return QString::fromUtf8(data); }

	void setText(const QString& text)
	{
		const QByteArray utf8 = text.toUtf8();
		strnzcpy(data, TStringView{ utf8.constData(), (size_t)utf8.size() }, (size_t)maxLen + 1);
		selectAll(True);
	}

	void handleEvent(TEvent& event) override
	{
		if (event.what == evKeyDown && (event.keyDown.keyCode == kbEnter || event.keyDown.keyCode == kbEsc || TKey{ event.keyDown } == kbCtrlC))
		{
			event.keyDown.keyCode == kbEnter ? onConfirmed() : onCancelled();
			clearEvent(event);
			return;
		}

		const QByteArray textBefore{ data };
		TInputLine::handleEvent(event);
		if (textBefore != data)
			onEdited(text());
	}

	// TInputLine's own palette indexes a dialog's
	TPalette& getPalette() const override
	{
		static constexpr TColorAttr entries[]{ ppFilterText, ppFilterText, ppFilterSelectedText, ppFilterArrows }; // Passive, active, selected text, arrows
		static TPalette palette{ entries };
		return palette;
	}
};

// One line of text under the file list
class CTuiStatusLine final : public TView
{
public:
	explicit CTuiStatusLine(const TRect& bounds) noexcept : TView(bounds)
	{
		growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
	}

	// A run of the text; a value, such as a number, a size or a date, takes the theme's value colour
	struct TextRun
	{
		QString text;
		bool value = false;
	};

	void setText(std::vector<TextRun> runs)
	{
		_runs = std::move(runs);
		drawView();
	}

	// Two rows: one that sets the text apart from the list, blank or the theme's divider line, then the text
	void draw() override
	{
		const TColorAttr color = mapColor(ppStatusText), valueColor = mapColor(ppStatusValue);
		TDrawBuffer line;
		if (appliedColourTheme().parameters.statusDividerLine)
			line.moveStr(0, stringView(QString{ size.x, u'─' }.toUtf8()), mapColor(ppStatusDivider));
		else
			line.moveChar(0, ' ', color, (ushort)size.x);
		writeLine(0, 0, (short)size.x, 1, line);

		line.moveChar(0, ' ', color, (ushort)size.x);
		ushort x = 1;
		for (const TextRun& run : _runs)
		{
			if (x >= size.x - 1)
				break;
			x = (ushort)(x + line.moveStr(x, stringView(run.text.toUtf8()), run.value ? valueColor : color, (ushort)(size.x - 1 - x)));
		}
		writeLine(0, 1, (short)size.x, 1, line);
	}

private:
	std::vector<TextRun> _runs;
};

// The side's tabs, wrapped onto rows, the active one highlighted.
// Past the rows shown, scrolled by row to keep the active tab in view, or by the mouse wheel until the tabs change.
class CTuiTabStrip final : public TView
{
public:
	struct Tab
	{
		qulonglong id;
		QString name;
	};

	static constexpr int maxRows = 3;

	explicit CTuiTabStrip(const TRect& bounds) noexcept : TView(bounds)
	{
		growMode = gfGrowHiX;
	}

	std::function<void(qulonglong tabId)> onTabClicked;

	void setTabs(const std::vector<Tab>& tabs, qulonglong activeTabId)
	{
		std::vector<Label> labels;
		std::optional<size_t> activeLabel;
		for (const Tab& tab : tabs)
		{
			if (tab.id == activeTabId)
				activeLabel = labels.size();
			labels.push_back({ tab.id, tab.name, cellWidth(tab.name) + 2 });
		}

		// Every refresh of the folder sets the tabs: the same ones keep the rows the wheel scrolled to
		if (labels == _labels && activeLabel == _activeLabel)
			return;

		_labels = std::move(labels);
		_activeLabel = activeLabel;
		_wheelFirstRow.reset();
		drawView();
	}

	// The rows the tabs take at this width, maxRows at most
	[[nodiscard]] int rowCount(const int width) const
	{
		return std::min(rowsOf(placementsFor(width)), maxRows);
	}

	void draw() override
	{
		const TColorAttr color = mapColor(ppStatusText), activeColor = mapColor(ppActiveTab);

		const std::vector<Placement> placements = placementsFor(size.x);
		const int firstRow = firstShownRow(placements), rows = rowsOf(placements);
		for (int y = 0; y < size.y; ++y)
		{
			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			for (size_t i = 0; i < _labels.size(); ++i)
			{
				const Placement& placement = placements[i];
				if (placement.row != firstRow + y)
					continue;

				const Label& label = _labels[i];
				// A label wider than the strip has a row to itself, its name shortened in the middle
				const QString shownName = placement.width < label.width ? elidedInTheMiddle(label.name, placement.width - 2) : label.name;
				line.moveStr((ushort)placement.x, stringView((u' ' + shownName + u' ').toUtf8()), i == _activeLabel ? activeColor : color, (ushort)placement.width);
			}

			if (y == 0 && firstRow > 0)
				line.moveStr((ushort)(size.x - 1), "▲", color);
			else if (y == size.y - 1 && firstRow + size.y < rows)
				line.moveStr((ushort)(size.x - 1), "▼", color);
			writeLine(0, (short)y, (short)size.x, 1, line);
		}
	}

	// The panel passes on the wheel over the strip
	void handleEvent(TEvent& event) override
	{
		TView::handleEvent(event);
		if (event.what == evMouseWheel && (event.mouse.wheel == mwUp || event.mouse.wheel == mwDown))
		{
			const std::vector<Placement> placements = placementsFor(size.x);
			_wheelFirstRow = std::clamp(firstShownRow(placements) + (event.mouse.wheel == mwUp ? -1 : 1), 0, lastFirstRow(placements));
			drawView();
			clearEvent(event);
			return;
		}

		if (event.what != evMouseDown)
			return;

		const TPoint point = makeLocal(event.mouse.where);
		const std::vector<Placement> placements = placementsFor(size.x);
		const int row = firstShownRow(placements) + point.y;
		for (size_t i = 0; i < placements.size(); ++i)
		{
			const Placement& placement = placements[i];
			if (placement.row == row && point.x >= placement.x && point.x < placement.x + placement.width)
			{
				onTabClicked(_labels[i].tabId);
				break;
			}
		}
		clearEvent(event);
	}

private:
	struct Label
	{
		qulonglong tabId = 0;
		QString name;
		int width = 0; // In cells, with a space on each side of the name

		[[nodiscard]] bool operator==(const Label&) const = default;
	};

	struct Placement
	{
		int row = 0, x = 0, width = 0;
	};

	[[nodiscard]] static int rowsOf(const std::vector<Placement>& placements)
	{
		return placements.empty() ? 0 : placements.back().row + 1;
	}

	// The last column holds the marks for hidden rows while there are any
	[[nodiscard]] std::vector<Placement> placementsFor(const int width) const
	{
		std::vector<Placement> placements = placementsAt(width);
		if (rowsOf(placements) > maxRows)
			placements = placementsAt(width - 1);
		return placements;
	}

	// Each label's place, in order: a label starts the next row when the rest of its row is too narrow
	[[nodiscard]] std::vector<Placement> placementsAt(const int width) const
	{
		std::vector<Placement> placements;
		int row = 0, x = 0;
		for (const Label& label : _labels)
		{
			const int labelWidth = std::min(label.width, width);
			if (x > 0 && x + labelWidth > width)
			{
				++row;
				x = 0;
			}
			placements.push_back({ row, x, labelWidth });
			x += labelWidth;
		}
		return placements;
	}

	[[nodiscard]] int lastFirstRow(const std::vector<Placement>& placements) const
	{
		return std::max(rowsOf(placements) - size.y, 0);
	}

	// The wheel's choice, else no rows scrolled off until the active label's row would be below the last row shown
	[[nodiscard]] int firstShownRow(const std::vector<Placement>& placements) const
	{
		if (_wheelFirstRow)
			return std::min(*_wheelFirstRow, lastFirstRow(placements));
		return _activeLabel ? std::max(placements[*_activeLabel].row - (size.y - 1), 0) : 0;
	}

private:
	std::vector<Label> _labels;
	std::optional<size_t> _activeLabel;
	std::optional<int> _wheelFirstRow; // Until the tabs change
};

// The keys a text field uses, which the panel binds itself: the menus see keys before the focused view, so they would take these from the filter line.
// 0 for any other key.
[[nodiscard]] static ushort commandForPanelKey(const KeyDownEvent& key)
{
	switch (key.keyCode)
	{
	case kbEnter: return cmActivateRowUnderCursor;
	case kbBack: return cmGoUp;
	case kbIns: return cmToggleSelectionAndMoveDown;
	default: break;
	}

	switch (key.charScan.charCode)
	{
	case ' ': return cmToggleSelectionCalculateSizeAndMoveDown;
	case '*': return cmInvertSelection;
	case '+': return cmSelectByPattern;
	case '-': return cmDeselectByPattern;
	case '=': return cmSelectByExtension;
	default: return 0;
	}
}

TRect panelBounds(TPoint desktopSize, Panel side)
{
	const int middle = desktopSize.x / 2;
	return side == Panel::LeftPanel ? TRect{ 0, 0, middle, desktopSize.y } : TRect{ middle, 0, desktopSize.x, desktopSize.y };
}

CTuiPanel::CTuiPanel(TPoint desktopSize, CController& controller, Panel side) :
	TWindowInit(&TWindow::initFrame),
	CTuiPathTitledWindow(panelBounds(desktopSize, side), {}),
	_controller(controller),
	_side(side)
{
	flags &= ~(wfMove | wfGrow | wfClose | wfZoom);
	state &= ~sfShadow; // Tiled: a shadow would fall on the other panel

	// layOutViews() sets the bounds
	_tabStrip = new CTuiTabStrip({});
	_tabStrip->onTabClicked = [this](qulonglong tabId) { activateTab(tabId); };
	insert(_tabStrip);
	_tabStrip->hide();

	_columnHeader = new CTuiColumnHeader({}, { FileTimeTextLength::Short, ppColumnHeader, ppHeaderColumnSeparator });
	_columnHeader->onColumnClicked = [this](FileListSortField field) { sortBy(field); };
	insert(_columnHeader);

	_fileList = new CTuiFileList({}, standardScrollBar(sbVertical));
	_fileList->onCursorMoved = [this](const CFileSystemObject& row) {
		_controller.setCurrentItemHashForCurrentFolder(_side, row.hash(), false);
		updateStatusLine();
		if (onCurrentItemChanged)
			onCurrentItemChanged();
	};
	insert(_fileList);

	_filterLine = new CTuiFilterLine({});
	_filterLine->onEdited = [this](const QString& text) { applyFilter(text); };
	_filterLine->onConfirmed = [this] {
		if (_filterLine->text().isEmpty())
			closeFilterLine();
		else
			_fileList->select();
	};
	_filterLine->onCancelled = [this] { closeFilterLine(); };
	insert(_filterLine);
	_filterLine->hide();

	_statusLine = new CTuiStatusLine({});
	insert(_statusLine);

	layOutViews();
	_fileList->select();

	// Through the controller so it attaches to every tab of the side
	_controller.setPanelContentsChangedListener(_side, this);
	fillFromPanel(); // The listing may have committed before this listener was attached
}

void CTuiPanel::calcBounds(TRect& bounds, TPoint)
{
	bounds = panelBounds(owner->size, _side);
}

void CTuiPanel::changeBounds(const TRect& bounds)
{
	TWindow::changeBounds(bounds);
	layOutViews(); // The tab strip's rows depend on the width
}

void CTuiPanel::handleEvent(TEvent& event)
{
	// Turbo Vision offers the wheel to every view regardless of the mouse's position: the panel on top would take it from the other, and its file list from its tab strip
	if (event.what == evMouseWheel)
	{
		if (!mouseInView(event.mouse.where))
			return;
		if ((_tabStrip->state & sfVisible) && _tabStrip->mouseInView(event.mouse.where))
		{
			_tabStrip->handleEvent(event);
			return;
		}
	}

	if (event.what == evMouseDown && !(state & sfSelected) && _fileList->mouseInView(event.mouse.where))
		_fileList->recordFirstClick(event);

	if (event.what == evBroadcast && event.message.command == cmTimerExpired && event.message.infoPtr == _showEmptyListTimer)
	{
		_showEmptyListTimer = nullptr;
		showEmptyListWhileListing();
		clearEvent(event);
		return;
	}

	TWindow::handleEvent(event);

	const ushort command = event.what == evKeyDown ? commandForPanelKey(event.keyDown) : event.what == evCommand ? event.message.command : 0;
	if (runCommand(command))
		clearEvent(event);
}

bool CTuiPanel::runCommand(const ushort command)
{
	if (command >= cmActivateTab1 && command < cmActivateTab1 + tabPositionCommandCount)
	{
		const std::vector<qulonglong> tabIds = _controller.tabIds(_side);
		if (const size_t position = command - cmActivateTab1; position < tabIds.size())
			activateTab(tabIds[position]);
		return true;
	}

	switch (command)
	{
	case cmNewTab: openTab(_controller.panel(_side).currentDirPathPosix()); break;
	case cmOpenFolderInNewTab:
		if (const CFileSystemObject* row = _fileList->currentRow(); row && row->isDir())
			openTab(row->fullAbsolutePath()); // For [..], the parent folder
		break;
	case cmReopenClosedTab:
		if (const QString path = _controller.takeLastClosedTabPath(_side); !path.isEmpty())
			openTab(path);
		break;
	case cmDuplicateTab:
		_controller.addTab(_side, _controller.panel(_side).currentDirPathPosix(), false, _controller.activeTabId(_side));
		updateTabStrip();
		break;
	case cmCloseTab:
		_controller.closeTab(_side, _controller.activeTabId(_side));
		showActiveTab();
		break;
	case cmCloseOtherTabs:
		for (const qulonglong tabId : _controller.tabIds(_side))
		{
			if (tabId != _controller.activeTabId(_side))
				_controller.closeTab(_side, tabId);
		}
		updateTabStrip();
		break;
	case cmNextTab: activateNeighbourTab(1); break;
	case cmPreviousTab: activateNeighbourTab(-1); break;
	case cmActivateRowUnderCursor: activateCurrentItem(); break;
	case cmGoUp: _controller.navigateUp(_side); break;
	case cmGoBack: _controller.navigateBack(_side); break;
	case cmGoForward: _controller.navigateForward(_side); break;
	case cmGoToFolder: goToTypedPath(); break;
	case cmGoToFileLocation: _controller.goToItem(_side, _controller.panel(_side).currentItem()); break;
	case cmCalculateOccupiedSpace: showOccupiedSpace(); break;
	case cmCompareFiles: compareFileWithOtherSide(); break;
	case cmShowFolderHistory: chooseRecentFolder(); break;
	case cmShowFavorites: showFavorites(); break;
	case cmShowAllFilesBelow: _controller.showAllFilesFromCurrentFolderAndBelow(_side); break;
	case cmJumpToFirstFile:
		if (const std::optional<int> row = _fileList->firstFileRow())
			_fileList->moveCursorTo(*row);
		break;
	case cmCalculateEachFolderSize: _controller.displayEachDirSize(_side); break;
	case cmRefresh: _controller.refreshPanelContents(_side); break;
	case cmToggleHiddenFiles: _controller.setShowHiddenFiles(!_controller.showHiddenFiles()); break;
	case cmOpenFilterLine: openFilterLine(); break;
	case cmSortByName: sortBy(FileListSortField::Name); break;
	case cmSortByExtension: sortBy(FileListSortField::Extension); break;
	case cmSortByDate: sortBy(FileListSortField::Date); break;
	case cmSortBySize: sortBy(FileListSortField::Size); break;
	case cmToggleSelectionCalculateSizeAndMoveDown:
		if (const CFileSystemObject* row = _fileList->currentRow())
			_controller.displayDirSize(_side, row->hash());
		[[fallthrough]];
	case cmToggleSelectionAndMoveDown:
		toggleCurrentItemSelection();
		_fileList->moveCursorTo(_fileList->cursorRow() + 1);
		break;
	case cmInvertSelection:
		_controller.panel(_side).invertSelection();
		showCoreSelection();
		break;
	case cmSelectByPattern:
	case cmDeselectByPattern:
		selectItemsByPattern(command == cmSelectByPattern);
		break;
	case cmSelectByExtension:
		if (const CFileSystemObject* row = _fileList->currentRow())
		{
			_controller.panel(_side).selectFilesWithExtensionOf(row->hash());
			showCoreSelection();
		}
		break;
	case cmCopyPaths:
		if (const QString pathLines = _controller.panel(_side).selectedOrCurrentItemPathLines(); !pathLines.isEmpty())
			CTuiApplication::copyToClipboard(pathLines);
		break;
	case cmOpenShell: runShell(_controller.panel(_side).currentDirPathNative()); break;
	case cmCopyItems:
		transferSelectedItems(_controller, _side, TransferKind::Copy);
		showCoreSelection(); // A copy deselects its sources
		break;
	case cmMoveItems: transferSelectedItems(_controller, _side, TransferKind::Move); break;
	case cmDeleteItems: deleteSelectedItems(_controller, _side, true); break;
	case cmDeleteItemsPermanently: deleteSelectedItems(_controller, _side, false); break;
	case cmRenameItem: renameCurrentItem(); break;
	case cmBatchRename:
		if (const std::vector<CFileSystemObject> items = _controller.panel(_side).selectedItemsOrCurrentItem(); !items.empty())
		{
			runBatchRenameDialog(items);
			_controller.refreshUnwatchedViews();
		}
		break;
	case cmCreateFolder: createFolder(); break;
	case cmCreateFile: createFile(); break;
	case cmViewFile:
		if (const QString path = currentFilePath(); !path.isEmpty())
			viewFile(path);
		break;
	case cmEditFile:
		if (const QString path = currentFilePath(); !path.isEmpty())
			editFile(path);
		break;
	default: return false;
	}
	return true;
}

void CTuiPanel::setState(ushort aState, Boolean enable)
{
	TWindow::setState(aState, enable);
	if ((aState & sfSelected) && enable)
		_controller.activePanelChanged(_side);
}

TPalette& CTuiPanel::getPalette() const
{
	return panelPalette();
}

void CTuiPanel::onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation)
{
	if (isActiveTab(p, tabId))
		fillFromPanel(operation);
}

void CTuiPanel::onPanelContentsInvalidated(Panel p, qulonglong tabId)
{
	if (!isActiveTab(p, tabId))
		return;

	// The old frame stays up briefly: a listing that arrives by then replaces it with no blank frame between them
	static constexpr uint oldFrameHoldMs = 100;
	_fileList->clearRowsKeepingScreen();
	_listedView.reset();
	if (!_showEmptyListTimer)
		_showEmptyListTimer = setTimer(oldFrameHoldMs);
}

void CTuiPanel::showEmptyListWhileListing()
{
	_fileList->showEmptyRows();
	updateStatusLine();
	updateTitle();
	updateTabStrip();
}

void CTuiPanel::fillFromPanel(const FileListRefreshCause cause)
{
	killTimer(_showEmptyListTimer);
	_showEmptyListTimer = nullptr;

	const CPanel& panel = _controller.panel(_side);
	// The cursor stays on its row when its item is gone or hidden by the filter, and starts on the first row of a folder just entered.
	// The core's current item is always the row under the cursor.
	const bool navigated = cause == refreshCauseForwardNavigation || cause == refreshCauseCdUp;
	_columnHeader->setSorting(panel.sortField(), panel.sortOrder());

	CPanel::CommittedListing listing = panel.committedListing();
	const std::optional<ListedView> listedView = listing.isCommitted ? std::optional{ ListedView{ panel.id(), panel.navigationId() } } : std::nullopt;
	const bool refreshesShownView = listedView && listedView == _listedView;
	_listedView = listedView;

	_fileList->setContents({
		.items = std::move(listing.items),
		.listedFolder = std::move(listing.folder),
		.refreshesShownView = refreshesShownView,
		.sortField = panel.sortField(),
		.sortOrder = panel.sortOrder(),
		.nameFilter = panel.nameFilter(),
		.selectedItemHashes = panel.selectedItemHashes(),
	}, _controller.currentItemHashForFolder(_side, panel.currentDirPathPosix()), navigated ? 0 : _fileList->cursorRow());
	if (const CFileSystemObject* row = _fileList->currentRow())
		_controller.setCurrentItemHashForCurrentFolder(_side, row->hash(), false);

	updateStatusLine();
	updateTitle();
	updateTabStrip();
	if (onCurrentItemChanged)
		onCurrentItemChanged();
}

void CTuiPanel::showCoreSelection()
{
	_fileList->setSelectedItems(_controller.panel(_side).selectedItemHashes());
	updateStatusLine();
}

void CTuiPanel::updateStatusLine()
{
	const CPanel& panel = _controller.panel(_side);
	const QLocale locale;

	if (const FolderContentsSummary selected = panel.selectionSummary(); selected.numFiles + selected.numFolders > 0)
	{
		_statusLine->setText({
			{ QString::number(selected.numFiles), true }, { QStringLiteral(" files, ") },
			{ QString::number(selected.numFolders), true }, { QStringLiteral(" folders selected (") },
			{ locale.formattedDataSize((qint64)selected.size), true }, { QStringLiteral(")") },
		});
		return;
	}

	const CFileSystemObject* row = _fileList->currentRow();
	if (!row)
	{
		_statusLine->setText({});
		return;
	}
	if (row->isCdUp())
	{
		if (row->size() == 0)
			_statusLine->setText({ { QStringLiteral("..") } });
		else
			_statusLine->setText({ { QStringLiteral("..  ") }, { locale.formattedDataSize((qint64)row->size()), true } });
		return;
	}

	const QString sizeText = row->isDir() && row->size() == 0 ? QStringLiteral("<DIR>") : locale.formattedDataSize((qint64)row->size());
	_statusLine->setText({
		{ row->fullName().toString() + QStringLiteral("  ") }, { sizeText, true },
		{ QStringLiteral("  ") }, { fileTimeText(row->modificationTime(), FileTimeTextLength::Long), true },
	});
}

void CTuiPanel::activateCurrentItem()
{
	const CFileSystemObject* row = _fileList->currentRow();
	if (!row)
		return;

	const CController::ItemActivationResult result = _controller.itemActivated(row->hash(), _side);
	if (result.code != FileOperationResultCode::Ok)
		showMessage(QStringLiteral("Error"), result.errorText.isEmpty() ? QStringLiteral("Failed to open the item") : result.errorText);
	else if (result.isProgram)
	{
		if (auto ran = CTuiApplication::runInTerminal(result.pathToLaunch, {}, QFileInfo(result.pathToLaunch).absolutePath(), true); !ran)
			showMessage(QStringLiteral("Failed to start the program"), ran.error());
	}
	else if (!result.pathToLaunch.isEmpty())
		openDocument(result.pathToLaunch);
}

void CTuiPanel::toggleCurrentItemSelection()
{
	const CFileSystemObject* row = _fileList->currentRow();
	if (!row)
		return;

	// The core refuses [..]
	_controller.panel(_side).setItemsSelected({ row->hash() }, !_fileList->isSelected(row->hash()));
	showCoreSelection();
}

void CTuiPanel::selectItemsByPattern(const bool selected)
{
	const QString action = selected ? QStringLiteral("Select") : QStringLiteral("Deselect");
	DialogSpec prompt = textPrompt(action, { QStringLiteral("Names matching (separate patterns with ;)") }, QStringLiteral("*"), action);
	prompt.checkBoxLabel = QStringLiteral("~F~iles only");
	const DialogAnswer answer = execDialog(prompt);
	if (answer.button != size_t{ 0 })
		return;

	_controller.panel(_side).setItemsSelectedByPattern(answer.inputText, selected, answer.checked);
	showCoreSelection();
}

void CTuiPanel::renameCurrentItem()
{
	const CFileSystemObject* row = _fileList->currentRow();
	if (!row || row->isCdUp())
		return;

	// Copied: the list can refresh while the dialog is open
	const qulonglong hash = row->hash();
	const QString oldName = row->fullName().toString();
	const std::optional<QString> newName = askForText(QStringLiteral("Rename"), { QStringLiteral("New name for \"%1\":").arg(oldName), true }, oldName, QStringLiteral("Rename"));
	if (!newName)
		return;

	InlineRenameResult result = _controller.renameItem(_side, hash, *newName, InlineRenameReplacement::Ask);
	if (result.status == InlineRenameStatus::ReplacementRequired)
	{
		const UserMessage question = inlineRenameReplacementQuestion(result, *newName);
		if (!confirmed(question.title, question.text, QStringLiteral("Replace")))
			return;
		result = _controller.renameItem(_side, hash, *newName, InlineRenameReplacement::Replace);
	}

	if (const auto message = inlineRenameMessage(result, oldName, *newName))
		showUserMessage(*message);
}

void CTuiPanel::createFolder()
{
	const std::optional<QString> folderName = askForText(QStringLiteral("New folder"), { QStringLiteral("Name of the new folder (a/b/c creates a chain):") },
		currentItemName(), QStringLiteral("Create"));
	if (!folderName)
		return;

	if (const auto message = _controller.createFolder(_side, *folderName))
		showUserMessage(*message);
}

void CTuiPanel::createFile()
{
	const std::optional<QString> fileName = askForText(QStringLiteral("New file"), { QStringLiteral("Name of the new file:") }, currentItemName(), QStringLiteral("Create"));
	if (!fileName)
		return;

	if (const auto message = _controller.createFile(_side, *fileName))
		showUserMessage(*message);
}

void CTuiPanel::goToTypedPath()
{
	const std::optional<QString> path = askForText(QStringLiteral("Go to"), { QStringLiteral("Folder:") }, _controller.panel(_side).currentDirPathNative(), QStringLiteral("Go"));
	if (!path)
		return;

	if (const auto message = _controller.goToTypedPath(_side, *path))
		showUserMessage(*message);
}

void CTuiPanel::chooseRecentFolder()
{
	static constexpr size_t folderBeforeCurrent = 1; // The current folder is the most recent
	const std::vector<QString> folders = _controller.recentFolders(_side);
	QStringList rows;
	for (const QString& folder : folders)
		rows.push_back(toNativeSeparators(folder));

	if (const std::optional<size_t> chosen = chooseFromList(QStringLiteral("Folder history"), rows, folderBeforeCurrent, {}, QStringLiteral("Go to"), QStringLiteral("Close")))
		_controller.setPath(_side, folders[*chosen], refreshCauseOther);
}

void CTuiPanel::showFavorites()
{
	if (const std::optional<QString> location = runFavoritesDialog(_controller.favoriteLocations(), _controller.panel(_side).currentDirPathNative()))
	{
		if (const auto message = _controller.goToTypedPath(_side, *location))
			showUserMessage(*message);
	}
}

void CTuiPanel::showOccupiedSpace()
{
	std::vector<QString> paths;
	for (const CFileSystemObject& item : _controller.panel(_side).selectedItemsOrCurrentItem())
		paths.push_back(item.fullAbsolutePath());
	if (paths.empty())
		return;

	CFileStatisticsScan scan{ paths };
	const bool finished = waitWithProgressDialog(QStringLiteral("Statistics"), [&scan] { return scan.finished(); }, [&scan](const int width) {
		return QStringList{ QStringLiteral("Scanning:"), elidedInTheMiddle(toNativeSeparators(scan.currentFolder()), width) };
	});

	// The destructor cancels an unfinished scan
	if (!finished || !scan.result())
		return;

	const CFileStatisticsScan::Result& result = *scan.result();
	const FileStatistics& stats = result.stats;
	// Empty when no selected path is an existing file or folder
	if (stats.empty())
	{
		showMessage(QStringLiteral("Statistics"), paths.size() == 1 ? QStringLiteral("The path %1 doesn't exist.").arg(toNativeSeparators(paths.front()))
			: QStringLiteral("None of the selected items exist."));
		return;
	}

	const QLocale locale;
	const QStringList summary{
		QStringLiteral("Files: %1").arg(stats.files),
		QStringLiteral("Folders: %1").arg(stats.folders),
		QStringLiteral("Occupied space: %1 (%2 bytes)").arg(locale.formattedDataSize((qint64)stats.occupiedSpace), locale.toString((qulonglong)stats.occupiedSpace)),
		QStringLiteral("Scanned in %1 s").arg((double)result.elapsedMs * 1e-3, 0, 'f', 1),
		{},
		QStringLiteral("Largest files (%1):").arg(stats.largestFiles.size()),
	};

	QStringList sizes, times;
	qsizetype sizeWidth = 0, timeWidth = 0;
	for (const CFileSystemObject& file : stats.largestFiles)
	{
		sizeWidth = std::max(sizeWidth, sizes.emplace_back(locale.formattedDataSize((qint64)file.size())).size());
		timeWidth = std::max(timeWidth, times.emplace_back(fileTimeText(file.modificationTime(), FileTimeTextLength::Short)).size());
	}

	// Only the path is elided: chooseFromList elides a row in its middle, which holds the columns
	const int pathWidth = std::max(chooseFromListMaxItemWidth() - (int)(sizeWidth + timeWidth + 4), 1);
	QStringList rows;
	for (qsizetype i = 0; i < sizes.size(); ++i)
	{
		rows.push_back(sizes[i].rightJustified(sizeWidth) + QStringLiteral("  ") + times[i].leftJustified(timeWidth) + QStringLiteral("  ") +
			elidedInTheMiddle(toNativeSeparators(stats.largestFiles[(size_t)i].fullAbsolutePath()), pathWidth));
	}

	if (const std::optional<size_t> chosen = chooseFromList(QStringLiteral("Statistics"), rows, 0, summary, QStringLiteral("Go to"), QStringLiteral("Close")))
		_controller.goToItem(_side, stats.largestFiles[*chosen]);
}

void CTuiPanel::compareFileWithOtherSide()
{
	const Panel otherSide = CController::otherPanelPosition(_side);
	const auto files = filesToCompare(_controller.currentItem(_side), _controller.currentItem(otherSide), _controller.currentFolderPath(otherSide));
	if (!files)
	{
		showUserMessage(files.error());
		return;
	}

	CFileComparator comparator{ files->pathA, files->pathB };
	const bool finished = waitWithProgressDialog(QStringLiteral("Comparing files"), [&comparator] { return comparator.finished(); }, [&comparator](const int width) {
		return QStringList{ QStringLiteral("Comparing the selected files..."), progressBar(comparator.percent(), width) };
	});
	if (finished && comparator.result())
		showUserMessage(fileComparisonResultMessage(*comparator.result(), *files));
}

void CTuiPanel::chooseVolume()
{
	std::vector<VolumeInfo> volumes = _controller.volumes();
	std::erase_if(volumes, [](const VolumeInfo& volume) { return !volume.isReady || !volume.rootObjectInfo.isValid(); });
	if (volumes.empty())
		return;

	qsizetype pathWidth = 0, labelWidth = 0;
	for (const VolumeInfo& volume : volumes)
	{
		pathWidth = std::max(pathWidth, volume.rootObjectInfo.fullAbsolutePath().size());
		labelWidth = std::max(labelWidth, volume.volumeLabel.size());
	}

	const QLocale locale;
	const std::optional<VolumeInfo> currentVolume = _controller.currentVolumeInfo(_side);
	QStringList rows;
	size_t currentRow = 0;
	for (const VolumeInfo& volume : volumes)
	{
		if (currentVolume && currentVolume->id() == volume.id())
			currentRow = (size_t)rows.size();
		rows.push_back(QStringLiteral("%1  %2  %3 free of %4").arg(toNativeSeparators(volume.rootObjectInfo.fullAbsolutePath()).leftJustified(pathWidth),
			volume.volumeLabel.leftJustified(labelWidth), locale.formattedDataSize((qint64)volume.freeSize), locale.formattedDataSize((qint64)volume.volumeSize)));
	}

	const std::optional<size_t> chosen = chooseFromList(QStringLiteral("Volume"), rows, currentRow);
	if (!chosen)
		return;

	if (const auto message = _controller.switchToVolume(_side, volumes[*chosen].id()))
		showUserMessage(*message);
}

QString CTuiPanel::currentItemName() const
{
	const CFileSystemObject* row = _fileList->currentRow();
	return row && !row->isCdUp() ? row->fullName().toString() : QString{};
}

QString CTuiPanel::currentFilePath() const
{
	const CFileSystemObject* row = _fileList->currentRow();
	return row && row->isFile() ? row->fullAbsolutePath() : QString{};
}

void CTuiPanel::sortBy(FileListSortField field)
{
	const CPanel& panel = _controller.panel(_side);
	const Qt::SortOrder order = panel.sortField() == field && panel.sortOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
	_controller.setTabSorting(_side, _controller.activeTabId(_side), field, order);
	fillFromPanel();
}

void CTuiPanel::openTab(const QString& path)
{
	_controller.addTab(_side, path, true, _controller.activeTabId(_side));
	showActiveTab();
}

void CTuiPanel::activateTab(qulonglong tabId)
{
	_controller.setActiveTab(_side, tabId);
	showActiveTab();
}

void CTuiPanel::activateNeighbourTab(int offset)
{
	const std::vector<qulonglong> tabIds = _controller.tabIds(_side);
	const auto active = std::ranges::find(tabIds, _controller.activeTabId(_side));
	assert_and_return_r(active != tabIds.end(), );

	const auto count = (std::ptrdiff_t)tabIds.size();
	activateTab(tabIds[(size_t)(((active - tabIds.begin()) + offset + count) % count)]);
}

void CTuiPanel::showActiveTab()
{
	const QString nameFilter = _controller.panel(_side).nameFilter();
	_filterLine->setText(nameFilter);
	setViewShown(_filterLine, !nameFilter.isEmpty());
	_fileList->select();
	fillFromPanel();
}

void CTuiPanel::updateTabStrip()
{
	std::vector<CTuiTabStrip::Tab> tabs;
	for (const qulonglong tabId : _controller.tabIds(_side))
		tabs.push_back({ tabId, _controller.tabName(_side, tabId) });

	_tabStrip->setTabs(tabs, _controller.activeTabId(_side));
	tabs.size() > 1 ? _tabStrip->show() : _tabStrip->hide();
	if (_tabStrip->size.y != tabStripRows())
		layOutViews();
}

int CTuiPanel::tabStripRows() const
{
	return _tabStrip->state & sfVisible ? _tabStrip->rowCount(size.x - 2) : 0;
}

void CTuiPanel::openFilterLine()
{
	setViewShown(_filterLine, true);
	_filterLine->select();
}

void CTuiPanel::applyFilter(const QString& filterText)
{
	// The core deselects what the filter hides
	_controller.panel(_side).setNameFilter(filterText);
	fillFromPanel();
}

void CTuiPanel::closeFilterLine()
{
	_filterLine->setText({});
	applyFilter({});
	setViewShown(_filterLine, false);
	_fileList->select();
}

void CTuiPanel::setViewShown(TView* view, bool shown)
{
	if (((view->state & sfVisible) != 0) == shown)
		return;

	shown ? view->show() : view->hide();
	layOutViews();
}

void CTuiPanel::layOutViews()
{
	TRect interior = getExtent();
	interior.grow(-1, -1);

	const int headerRow = interior.a.y + tabStripRows();
	const int listTop = headerRow + 1;
	const int statusTop = interior.b.y - 2; // The status line's blank row
	const int filterRow = statusTop - 1;
	_tabStrip->changeBounds({ interior.a.x, interior.a.y, interior.b.x, headerRow });
	_columnHeader->changeBounds({ interior.a.x, headerRow, interior.b.x, headerRow + 1 });
	_statusLine->changeBounds({ interior.a.x, statusTop, interior.b.x, interior.b.y });
	_filterLine->changeBounds({ interior.a.x, filterRow, interior.b.x, statusTop });
	_fileList->changeBounds({ interior.a.x, listTop, interior.b.x, _filterLine->state & sfVisible ? filterRow : statusTop });
	// Each view drew itself as it moved, but not where another one still covered it then
	redraw();
}

void CTuiPanel::updateTitle()
{
	const CPanel& panel = _controller.panel(_side);
	setTitle(*this, panel.displayMode() == CPanel::SearchResultsMode ? QStringLiteral("Search results: ") + panel.currentDirPathNative() : panel.currentDirPathNative());
}

bool CTuiPanel::isActiveTab(Panel p, qulonglong tabId) const
{
	return p == _side && tabId == _controller.activeTabId(_side);
}

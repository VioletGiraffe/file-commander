#include "ctuipanel.h"
#include "ctuiapplication.h"
#include "ctuibatchrenamedialog.h"
#include "ctuidialogs.h"
#include "ctuifilelist.h"
#include "ctuifileoperations.h"
#include "ctuipalette.h"
#include "ctuiprograms.h"

#include "ccontroller.h"
#include "filelist/filetimetext.h"
#include "filesystemhelperfunctions.h"

#include "fileoperations/fileoperationtexts.h"

#include "assert/advanced_assert.h"

#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TFrame
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TScrollBar
#define Uses_TView
#define Uses_MsgBox
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QFileInfo>
#include <QLocale>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <optional>
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
		if (event.what == evKeyDown && (event.keyDown.keyCode == kbEnter || event.keyDown.keyCode == kbEsc))
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

	void setText(const QString& text)
	{
		_text = text.toUtf8();
		drawView();
	}

	void draw() override
	{
		const TColorAttr color = mapColor(ppStatusText);

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		line.moveStr(1, TStringView{ _text.constData(), (size_t)_text.size() }, color, (ushort)std::max(size.x - 2, 0));
		writeLine(0, 0, (short)size.x, 1, line);
	}

private:
	QByteArray _text;
};

// The side's tabs on one row, the active one highlighted. Scrolled to keep the active tab in view.
class CTuiTabStrip final : public TView
{
public:
	struct Tab
	{
		qulonglong id;
		QString name;
	};

	explicit CTuiTabStrip(const TRect& bounds) noexcept : TView(bounds)
	{
		growMode = gfGrowHiX;
	}

	std::function<void(qulonglong tabId)> onTabClicked;

	void setTabs(const std::vector<Tab>& tabs, qulonglong activeTabId)
	{
		_labels.clear();
		_activeLabel.reset();
		int x = 0;
		for (const Tab& tab : tabs)
		{
			if (tab.id == activeTabId)
				_activeLabel = _labels.size();

			Label& label = _labels.emplace_back();
			label.tabId = tab.id;
			label.text = (u' ' + tab.name + u' ').toUtf8();
			label.x = x;
			label.width = strwidth(TStringView{ label.text.constData(), (size_t)label.text.size() });
			x += label.width;
		}
		drawView();
	}

	void draw() override
	{
		const TColorAttr color = mapColor(ppStatusText), activeColor = mapColor(ppActiveTab);

		const int offset = scrollOffset();
		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		for (size_t i = 0; i < _labels.size(); ++i)
		{
			const Label& label = _labels[i];
			const int start = label.x - offset;
			if (start + label.width <= 0 || start >= size.x)
				continue;

			const int indent = std::max(start, 0);
			line.moveStr((ushort)indent, TStringView{ label.text.constData(), (size_t)label.text.size() }, i == _activeLabel ? activeColor : color,
				(ushort)(size.x - indent), (ushort)(indent - start));
		}
		writeLine(0, 0, (short)size.x, 1, line);
	}

	void handleEvent(TEvent& event) override
	{
		TView::handleEvent(event);
		if (event.what != evMouseDown)
			return;

		const int x = makeLocal(event.mouse.where).x + scrollOffset();
		for (const Label& label : _labels)
		{
			if (x >= label.x && x < label.x + label.width)
			{
				onTabClicked(label.tabId);
				break;
			}
		}
		clearEvent(event);
	}

private:
	// Columns scrolled off on the left: none until the active label would end past the right edge
	[[nodiscard]] int scrollOffset() const
	{
		if (!_activeLabel)
			return 0;

		const Label& active = _labels[*_activeLabel];
		return std::clamp(active.x + active.width - size.x, 0, active.x);
	}

	struct Label
	{
		qulonglong tabId = 0;
		QByteArray text;
		int x = 0, width = 0;
	};

	std::vector<Label> _labels;
	std::optional<size_t> _activeLabel;
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
	case ' ': return cmToggleSelectionAndCalculateSize;
	case '*': return cmInvertSelection;
	case '+': return cmSelectByPattern;
	case '-': return cmDeselectByPattern;
	case '=': return cmSelectByExtension;
	default: return 0;
	}
}

CTuiPanel::CTuiPanel(const TRect& bounds, CController& controller, Panel side) :
	TWindowInit(&TWindow::initFrame),
	TWindow(bounds, {}, wnNoNumber),
	_controller(controller),
	_side(side)
{
	flags &= ~(wfMove | wfGrow | wfClose | wfZoom);
	state &= ~sfShadow; // Tiled: a shadow would fall on the other panel
	growMode = gfGrowAll | gfGrowRel;

	// layOutViews() sets the bounds
	_tabStrip = new CTuiTabStrip({});
	_tabStrip->onTabClicked = [this](qulonglong tabId) { activateTab(tabId); };
	insert(_tabStrip);
	_tabStrip->hide();

	_columnHeader = new CTuiColumnHeader({});
	_columnHeader->onColumnClicked = [this](FileListSortField field) { sortBy(field); };
	insert(_columnHeader);

	_fileList = new CTuiFileList({}, standardScrollBar(sbVertical));
	_fileList->onCursorMoved = [this](const CFileSystemObject& row) {
		_controller.setCurrentItemHashForCurrentFolder(_side, row.hash(), false);
		updateStatusLine();
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

void CTuiPanel::handleEvent(TEvent& event)
{
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
	case cmRefresh: _controller.refreshPanelContents(_side); break;
	case cmToggleHiddenFiles: _controller.setShowHiddenFiles(!_controller.showHiddenFiles()); break;
	case cmOpenFilterLine: openFilterLine(); break;
	case cmSortByName: sortBy(FileListSortField::Name); break;
	case cmSortByExtension: sortBy(FileListSortField::Extension); break;
	case cmSortByDate: sortBy(FileListSortField::Date); break;
	case cmSortBySize: sortBy(FileListSortField::Size); break;
	case cmToggleSelectionAndMoveDown:
		toggleCurrentItemSelection();
		_fileList->moveCursorTo(_fileList->cursorRow() + 1);
		break;
	case cmToggleSelectionAndCalculateSize:
		toggleCurrentItemSelection();
		if (const CFileSystemObject* row = _fileList->currentRow())
			_controller.displayDirSize(_side, row->hash());
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

	_fileList->setRows({}, {}, 0, 0);
	_fileList->setSelectedItems({});
	updateStatusLine();
	updateTitle();
	updateTabStrip();
}

void CTuiPanel::fillFromPanel(const FileListRefreshCause cause)
{
	const CPanel& panel = _controller.panel(_side);
	// The cursor stays on its row when its item is gone or hidden by the filter, and starts on the first row of a folder just entered.
	// The core's current item is always the row under the cursor.
	const bool navigated = cause == refreshCauseForwardNavigation || cause == refreshCauseCdUp;
	_columnHeader->setSorting(panel.sortField(), panel.sortOrder());
	_fileList->setSortField(panel.sortField());
	_fileList->setRows(panel.visibleItems(), panel.currentDirPathPosix(), _controller.currentItemHashForFolder(_side, panel.currentDirPathPosix()),
		navigated ? 0 : _fileList->cursorRow());
	if (const CFileSystemObject* row = _fileList->currentRow())
		_controller.setCurrentItemHashForCurrentFolder(_side, row->hash(), false);

	showCoreSelection();
	updateTitle();
	updateTabStrip();
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
		_statusLine->setText(QStringLiteral("%1 files, %2 folders selected (%3)").arg(selected.numFiles).arg(selected.numFolders).arg(locale.formattedDataSize((qint64)selected.size)));
		return;
	}

	const CFileSystemObject* row = _fileList->currentRow();
	if (!row || row->isCdUp())
	{
		_statusLine->setText(row ? QStringLiteral("..") : QString{});
		return;
	}

	const QString sizeText = row->isDir() && row->size() == 0 ? QStringLiteral("<DIR>") : locale.formattedDataSize((qint64)row->size());
	_statusLine->setText(row->fullName().toString() + QStringLiteral("  ") + sizeText + QStringLiteral("  ") + fileTimeText(row->modificationTime(), FileTimeTextLength::Long));
}

void CTuiPanel::activateCurrentItem()
{
	const CFileSystemObject* row = _fileList->currentRow();
	if (!row)
		return;

	const CController::ItemActivationResult result = _controller.itemActivated(row->hash(), _side);
	if (result.code != FileOperationResultCode::Ok)
	{
		const QByteArray text = (result.errorText.isEmpty() ? QStringLiteral("Failed to open the item") : result.errorText).toUtf8();
		messageBox(TStringView{ text.constData(), (size_t)text.size() }, mfError | mfOKButton);
	}
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

	const bool shown = tabs.size() > 1;
	_tabStrip->setTabs(tabs, _controller.activeTabId(_side));
	setViewShown(_tabStrip, shown);
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

	const int headerRow = _tabStrip->state & sfVisible ? interior.a.y + 1 : interior.a.y;
	const int listTop = headerRow + 1;
	const int statusRow = interior.b.y - 1;
	const int filterRow = statusRow - 1;
	_tabStrip->changeBounds({ interior.a.x, interior.a.y, interior.b.x, interior.a.y + 1 });
	_columnHeader->changeBounds({ interior.a.x, headerRow, interior.b.x, headerRow + 1 });
	_statusLine->changeBounds({ interior.a.x, statusRow, interior.b.x, statusRow + 1 });
	_filterLine->changeBounds({ interior.a.x, filterRow, interior.b.x, statusRow });
	_fileList->changeBounds({ interior.a.x, listTop, interior.b.x, _filterLine->state & sfVisible ? filterRow : statusRow });
}

void CTuiPanel::updateTitle()
{
	const CPanel& panel = _controller.panel(_side);
	// The label comes first: the frame cuts a long title at its end
	const QString text = panel.displayMode() == CPanel::SearchResultsMode ? QStringLiteral("Search results: ") + panel.currentDirPathNative() : panel.currentDirPathNative();
	const QByteArray utf8 = text.toUtf8();
	delete[] const_cast<char*>(title);
	title = newStr(TStringView{ utf8.constData(), (size_t)utf8.size() });
	if (frame)
		frame->drawView();
}

bool CTuiPanel::isActiveTab(Panel p, qulonglong tabId) const
{
	return p == _side && tabId == _controller.activeTabId(_side);
}

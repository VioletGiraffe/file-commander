#include "ctuipanel.h"
#include "ctuidialogs.h"
#include "ctuifilelist.h"
#include "ctuifileoperations.h"

#include "ccontroller.h"
#include "filesystemhelperfunctions.h"

#include "fileoperations/fileoperationtexts.h"

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
#include <QDateTime>
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

	void clear()
	{
		data[0] = EOS;
		selectAll(False);
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

	// BIOS colours: the input line palette indexes a dialog's palette, which a window does not have
	TColorAttr mapColor(uchar index) noexcept override
	{
		switch (index)
		{
		case 3: return 0x1F; // Selected text
		case 4: return 0x3A; // Scroll arrows
		default: return 0x30;
		}
	}
};

// One line of text under the file list, with a second part aligned to the right
class CTuiStatusLine final : public TView
{
public:
	explicit CTuiStatusLine(const TRect& bounds) noexcept : TView(bounds)
	{
		growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
	}

	void setText(const QString& left, const QString& right)
	{
		_left = left.toUtf8();
		_right = right.toUtf8();
		drawView();
	}

	void draw() override
	{
		static constexpr TColorAttr color{ 0x17 };

		const TStringView right{ _right.constData(), (size_t)_right.size() };
		const int rightStart = std::max(size.x - 1 - strwidth(right), 0);

		TDrawBuffer line;
		line.moveChar(0, ' ', color, (ushort)size.x);
		line.moveStr(1, TStringView{ _left.constData(), (size_t)_left.size() }, color, (ushort)std::max(rightStart - 2, 0));
		line.moveStr((ushort)rightStart, right, color);
		writeLine(0, 0, (short)size.x, 1, line);
	}

private:
	QByteArray _left, _right;
};

[[nodiscard]] static QString sortFieldName(FileListSortField field)
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

CTuiPanel::CTuiPanel(const TRect& bounds, CController& controller, Panel side) :
	TWindowInit(&TWindow::initFrame),
	TWindow(bounds, {}, wnNoNumber),
	_controller(controller),
	_side(side)
{
	flags &= ~(wfMove | wfGrow | wfClose | wfZoom);
	growMode = gfGrowAll | gfGrowRel;

	// layOutViews() sets the bounds
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

	if (event.what == evKeyDown)
	{
		switch (event.keyDown.keyCode)
		{
		case kbEnter:  activateCurrentItem(); break;
		case kbBack:   _controller.navigateUp(_side); break;
		case kbIns:
			toggleCurrentItemSelection();
			_fileList->moveCursorTo(_fileList->cursorRow() + 1);
			break;
		case kbCtrlF:  openFilterLine(); break;
		case kbCtrlR:  _controller.refreshPanelContents(_side); break;
		case kbCtrlS:  _controller.setShowHiddenFiles(!_controller.showHiddenFiles()); break;
		case kbAltLeft:  _controller.navigateBack(_side); break;
		case kbAltRight: _controller.navigateForward(_side); break;
		case kbAltD:   goToTypedPath(); break;
		case kbCtrlF3: sortBy(FileListSortField::Name); break;
		case kbCtrlF4: sortBy(FileListSortField::Extension); break;
		case kbCtrlF5: sortBy(FileListSortField::Date); break;
		case kbCtrlF6: sortBy(FileListSortField::Size); break;
		default:
			if (event.keyDown.charScan.charCode == ' ')
			{
				toggleCurrentItemSelection();
				if (const CFileSystemObject* row = _fileList->currentRow())
					_controller.displayDirSize(_side, row->hash());
			}
			else if (event.keyDown.charScan.charCode == '*')
			{
				_controller.panel(_side).invertSelection();
				showCoreSelection();
			}
			else
				return;
		}
		clearEvent(event);
	}
	else if (event.what == evCommand)
	{
		switch (event.message.command)
		{
		case cmActivateFileListItem: activateCurrentItem(); break;
		case cmCopyItems:
			transferSelectedItems(_controller, _side, TransferKind::Copy);
			showCoreSelection(); // A copy deselects its sources
			break;
		case cmMoveItems: transferSelectedItems(_controller, _side, TransferKind::Move); break;
		case cmDeleteItems: deleteSelectedItems(_controller, _side, true); break;
		case cmDeleteItemsPermanently: deleteSelectedItems(_controller, _side, false); break;
		case cmRenameItem: renameCurrentItem(); break;
		case cmCreateFolder: createFolder(); break;
		case cmCreateFile: createFile(); break;
		default: return;
		}
		clearEvent(event);
	}
}

void CTuiPanel::setState(ushort aState, Boolean enable)
{
	TWindow::setState(aState, enable);
	if ((aState & sfSelected) && enable)
		_controller.activePanelChanged(_side);
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

	_fileList->setRows({}, 0, 0);
	_fileList->setSelectedItems({});
	updateStatusLine();
	setTitle(_controller.panel(_side).currentDirPathNative());
}

void CTuiPanel::fillFromPanel(const FileListRefreshCause cause)
{
	const CPanel& panel = _controller.panel(_side);
	// The cursor stays on its row when its item is gone or hidden by the filter, and starts on the first row of a folder just entered.
	// The core's current item is always the row under the cursor.
	const bool navigated = cause == refreshCauseForwardNavigation || cause == refreshCauseCdUp;
	_fileList->setRows(panel.visibleItems(), _controller.currentItemHashForFolder(_side, panel.currentDirPathPosix()), navigated ? 0 : _fileList->cursorRow());
	if (const CFileSystemObject* row = _fileList->currentRow())
		_controller.setCurrentItemHashForCurrentFolder(_side, row->hash(), false);

	showCoreSelection();
	setTitle(panel.currentDirPathNative());
}

void CTuiPanel::showCoreSelection()
{
	_fileList->setSelectedItems(_controller.panel(_side).selectedItemHashes());
	updateStatusLine();
}

void CTuiPanel::updateStatusLine()
{
	const CPanel& panel = _controller.panel(_side);
	const QString sorting = sortFieldName(panel.sortField()) + (panel.sortOrder() == Qt::AscendingOrder ? QStringLiteral(" ↑") : QStringLiteral(" ↓"));
	const QLocale locale;

	if (const FolderContentsSummary selected = panel.selectionSummary(); selected.numFiles + selected.numFolders > 0)
	{
		_statusLine->setText(QStringLiteral("%1 files, %2 folders selected (%3)").arg(selected.numFiles).arg(selected.numFolders).arg(locale.formattedDataSize((qint64)selected.size)), sorting);
		return;
	}

	const CFileSystemObject* row = _fileList->currentRow();
	if (!row || row->isCdUp())
	{
		_statusLine->setText(row ? QStringLiteral("..") : QString{}, sorting);
		return;
	}

	const QString sizeText = row->isDir() && row->size() == 0 ? QStringLiteral("<DIR>") : locale.formattedDataSize((qint64)row->size());
	const QString modified = QDateTime::fromSecsSinceEpoch(row->modificationTime()).toString(QStringLiteral("dd.MM.yyyy hh:mm"));
	_statusLine->setText(row->fullName().toString() + QStringLiteral("  ") + sizeText + QStringLiteral("  ") + modified, sorting);
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
	else if (!result.pathToOpen.isEmpty())
		messageBox("Opening documents is not supported yet", mfInformation | mfOKButton);
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

	InlineRenameResult result = _controller.renameItem(_side, hash, *newName, false);
	if (result.status == InlineRenameStatus::ReplacementRequired)
	{
		const UserMessage question = inlineRenameReplacementQuestion(result, *newName);
		if (!confirmed(question.title, question.text, QStringLiteral("Replace")))
			return;
		result = _controller.renameItem(_side, hash, *newName, true);
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

void CTuiPanel::sortBy(FileListSortField field)
{
	const CPanel& panel = _controller.panel(_side);
	const Qt::SortOrder order = panel.sortField() == field && panel.sortOrder() == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder;
	_controller.setTabSorting(_side, _controller.activeTabId(_side), field, order);
	fillFromPanel();
}

void CTuiPanel::openFilterLine()
{
	if (_filterLine->state & sfVisible)
	{
		_filterLine->select();
		return;
	}

	_filterLine->show();
	layOutViews();
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
	_filterLine->clear();
	applyFilter({});
	_filterLine->hide();
	layOutViews();
	_fileList->select();
}

void CTuiPanel::layOutViews()
{
	TRect interior = getExtent();
	interior.grow(-1, -1);

	const int statusRow = interior.b.y - 1;
	const int filterRow = statusRow - 1;
	_statusLine->changeBounds({ interior.a.x, statusRow, interior.b.x, statusRow + 1 });
	_filterLine->changeBounds({ interior.a.x, filterRow, interior.b.x, statusRow });
	_fileList->changeBounds({ interior.a.x, interior.a.y, interior.b.x, _filterLine->state & sfVisible ? filterRow : statusRow });
}

void CTuiPanel::setTitle(const QString& text)
{
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

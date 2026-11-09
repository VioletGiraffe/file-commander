#include "ctuipanel.h"
#include "ctuifilelist.h"

#include "ccontroller.h"

#define Uses_TEvent
#define Uses_TFrame
#define Uses_TKeys
#define Uses_TScrollBar
#define Uses_MsgBox
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {
// The parent entry first, then folders, then files; case-insensitive by name within each
// Prototype only: the GUI's sort rules live in CFileListModel
void sortRows(std::vector<CFileSystemObject>& rows)
{
	const auto group = [](const CFileSystemObject& item) { return item.isCdUp() ? 0 : item.isDir() ? 1 : 2; };
	std::ranges::sort(rows, [&group](const CFileSystemObject& l, const CFileSystemObject& r) {
		if (const int lGroup = group(l), rGroup = group(r); lGroup != rGroup)
			return lGroup < rGroup;
		return l.fullName().compare(r.fullName(), Qt::CaseInsensitive) < 0;
	});
}
}

CTuiPanel::CTuiPanel(const TRect& bounds, CController& controller, Panel side) :
	TWindowInit(&TWindow::initFrame),
	TWindow(bounds, {}, wnNoNumber),
	_controller(controller),
	_side(side)
{
	flags &= ~(wfMove | wfGrow | wfClose | wfZoom);
	growMode = gfGrowAll | gfGrowRel;

	TRect listBounds = getExtent();
	listBounds.grow(-1, -1);
	_fileList = new CTuiFileList(listBounds, standardScrollBar(sbVertical));
	_fileList->onCursorMoved = [this](const CFileSystemObject& row) {
		_controller.setCurrentItemHashForCurrentFolder(_side, row.hash(), false);
	};
	insert(_fileList);

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
		case kbEnter: activateCurrentItem(); break;
		case kbBack:  _controller.navigateUp(_side); break;
		default: return;
		}
		clearEvent(event);
	}
	else if (event.what == evCommand && event.message.command == cmActivateFileListItem)
	{
		activateCurrentItem();
		clearEvent(event);
	}
}

void CTuiPanel::setState(ushort aState, Boolean enable)
{
	TWindow::setState(aState, enable);
	if ((aState & sfSelected) && enable)
		_controller.activePanelChanged(_side);
}

void CTuiPanel::onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause /*operation*/)
{
	if (isActiveTab(p, tabId))
		fillFromPanel();
}

void CTuiPanel::onPanelContentsInvalidated(Panel p, qulonglong tabId)
{
	if (!isActiveTab(p, tabId))
		return;

	_fileList->setRows({}, 0);
	setTitle(_controller.panel(_side).currentDirPathNative());
}

void CTuiPanel::fillFromPanel()
{
	const CPanel& panel = _controller.panel(_side);
	std::vector<CFileSystemObject> rows;
	panel.readCommittedContents([&rows](const QString& /*folder*/, const FileListHashMap& contents) {
		rows.reserve(contents.size());
		for (const auto& item : contents)
			rows.push_back(item.second);
	});

	sortRows(rows);
	_fileList->setRows(std::move(rows), _controller.currentItemHashForFolder(_side, panel.currentDirPathPosix()));
	if (const CFileSystemObject* row = _fileList->currentRow())
		_controller.setCurrentItemHashForCurrentFolder(_side, row->hash(), false);

	setTitle(panel.currentDirPathNative());
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

#include "cpanelwidget.h"
#include "filelistwidget/cfilelistfilterdialog.h"
#include "filelistwidget/model/cfilelistmodel.h"
#include "shell/cshell.h"
#include "columns.h"
#include "filelistwidget/model/cfilelistsortfilterproxymodel.h"
#include "pluginengine/cpluginengine.h"
#include "../favoritelocationseditor/cfavoritelocationseditor.h"
#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"
#include "filesystemhelpers/filesystemhelpers.hpp"
#include "fileoperationresultcode.h" // Was reached transitively through the now-removed cfilemanipulator.h
#include "fileoperations/inlinerename.h"
#include "progressdialogs/progressdialoghelpers.h"
#include "settings/csettings.h"
#include "settings.h"
#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "widgets/clineedit.h"
#include "widgets/layouts/cflowlayout.h"

#include "system/ctimeelapsed.h"
#include "detail/hashmap_helpers.h"

#include <3rdparty/ankerl/unordered_dense.h>

DISABLE_COMPILER_WARNINGS
#include "ui_cpanelwidget.h"

#include <QClipboard>
#include <QCompleter>
#include <QDebug>
#include <QHeaderView>
#include <QHelpEvent>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QTabBar>
#include <QToolTip>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <assert.h>
#include <unordered_set>

struct FolderContentsSummary {
	uint64_t numFiles = 0;
	uint64_t numFolders = 0;
	uint64_t size = 0; // Only counts files' sizes, folder sizes are unknown without explicit calculation
};

static FolderContentsSummary summarizeFolderContents(const FileListHashMap& items)
{
	FolderContentsSummary summary;
	for (const auto& item: items)
	{
		const CFileSystemObject& object = item.second;
		if (object.isCdUp())
			continue;

		if (object.isFile())
			++summary.numFiles;
		else if (object.isDir())
			++summary.numFolders;

		summary.size += object.size();
	}

	return summary;
}

CPanelWidget::CPanelWidget(QWidget *parent) noexcept :
	QWidget(parent),
	ui(new Ui::CPanelWidget)
{
	ui->setupUi(this);

	ui->_infoLabel->clear();
	ui->_driveInfoLabel->clear();

	ui->_pathNavigator->setLineEdit(new CLineEdit);
	ui->_pathNavigator->lineEdit()->setFocusPolicy(Qt::ClickFocus);

	auto* completer = ui->_pathNavigator->completer();
	completer->setCompletionMode(QCompleter::PopupCompletion);
	completer->setFilterMode(Qt::MatchContains);

	ui->_pathNavigator->setHistoryMode(true);
	ui->_pathNavigator->installEventFilter(this);
	assert_r(connect(ui->_pathNavigator, &CHistoryComboBox::textActivated, this, &CPanelWidget::pathFromHistoryActivated));
	assert_r(connect(ui->_pathNavigator, &CHistoryComboBox::itemActivated, this, &CPanelWidget::pathFromHistoryActivated));

	assert_r(connect(ui->_list, &CFileListView::contextMenuRequested, this, &CPanelWidget::showContextMenuForItems));
	assert_r(connect(ui->_list, &CFileListView::keyPressed, this, &CPanelWidget::fileListViewKeyPressed));
	assert_r(connect(ui->_list, &CFileListView::itemMiddleClicked, this, &CPanelWidget::onItemMiddleClicked));

	assert_r(connect(ui->_driveInfoLabel, &CClickableLabel::doubleClicked, this, &CPanelWidget::showFavoriteLocationsMenu));
	assert_r(connect(ui->_btnFavs, &QPushButton::clicked, this, [&]{showFavoriteLocationsMenu(mapToGlobal(ui->_btnFavs->geometry().bottomLeft()));}));
	assert_r(connect(ui->_btnToRoot, &QToolButton::clicked, this, &CPanelWidget::toRoot));

	_filterDialog = new CFileListFilterDialog(ui->_list);
	assert_r(connect(_filterDialog, &CFileListFilterDialog::filterTextEdited, this, &CPanelWidget::filterTextEdited));
	assert_r(connect(_filterDialog, &CFileListFilterDialog::filterTextConfirmed, this, &CPanelWidget::filterTextConfirmed));
	ui->_list->installEventFilter(_filterDialog);

	ui->_list->addEventObserver(this);

	onSettingsChanged();

	new QShortcut(QKeySequence(Qt::Key_Space), this, SLOT(onSpacePressed()), nullptr, Qt::WidgetWithChildrenShortcut);
	new QShortcut(QKeySequence(Qt::Key_Insert), this, SLOT(invertCurrentItemSelection()), nullptr, Qt::WidgetWithChildrenShortcut);
	new QShortcut(QKeySequence(QSL("Ctrl+C")), this, SLOT(copySelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut);
	new QShortcut(QKeySequence(QSL("Ctrl+X")), this, SLOT(cutSelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut);
	new QShortcut(QKeySequence(QSL("Ctrl+V")), this, SLOT(pasteSelectionFromClipboard()), nullptr, Qt::WidgetWithChildrenShortcut);
	new QShortcut(QKeySequence(QSL("Ctrl+Shift+V")), this, [this] { pasteSelectionFromClipboard(true); }, Qt::WidgetWithChildrenShortcut);

	for (int i = 1; i <= 9; ++i)
		new QShortcut(QKeySequence(QSL("Ctrl+%1").arg(i)), this, [this, i] { switchToTabByPosition(i - 1); }, Qt::WidgetWithChildrenShortcut);
}

CPanelWidget::~CPanelWidget() noexcept
{
	delete ui;
}

void CPanelWidget::init(CController* controller)
{
	assert_debug_only(controller);
	_controller = controller;
}

void CPanelWidget::setFocusToFileList()
{
	ui->_list->setFocus();
}

QByteArray CPanelWidget::savePanelState() const
{
	return ui->_list->header()->saveState();
}

bool CPanelWidget::restorePanelState(const QByteArray& state)
{
	if (!state.isEmpty())
	{
		ui->_list->setHeaderAdjustmentRequired(false);
		const bool ok = ui->_list->header()->restoreState(state);
		// Mirror the persisted blob into the startup-active tab's own storage, otherwise switching away
		// from and back to it (before ever resizing anything) would revert to its stale, pre-restore seed.
		if (_activeTab >= 0 && _activeTab < (int)_tabs.size())
			_tabs[(size_t)_activeTab].headerState = state;
		return ok;
	}
	else
	{
		ui->_list->setHeaderAdjustmentRequired(true);
		return false;
	}
}

QByteArray CPanelWidget::savePanelGeometry() const
{
	return ui->_list->header()->saveGeometry();
}

bool CPanelWidget::restorePanelGeometry(const QByteArray& state)
{
	return ui->_list->header()->restoreGeometry(state);
}

QString CPanelWidget::currentDirPathNative() const
{
	return _controller->panel(_panelPosition).currentDirPathNative();
}

Panel CPanelWidget::panelPosition() const
{
	return _panelPosition;
}

void CPanelWidget::initPanel(Panel p)
{
	assert_r(_panelPosition == Panel::UnknownPanel && _tabs.empty());
	_panelPosition = p;

	ui->_list->installEventFilter(this);
	ui->_list->viewport()->installEventFilter(this);
	ui->_list->setPanelPosition(p);

	ui->_tabBar->installEventFilter(this); // For closing tabs on middle click
	ui->_tabBar->setExpanding(false);
	ui->_tabBar->setTabsClosable(true);
	ui->_tabBar->setMovable(true);
	ui->_tabBar->setElideMode(Qt::ElideMiddle);
	ui->_tabBar->setDrawBase(false);
	ui->_tabBar->setStyleSheet("QTabBar::tab { height: 32px; }");
	// QTabBar defaults to Qt::TabFocus; keep it out of the Tab focus chain so Tab keeps toggling the two panels (Ctrl+Tab cycles tabs).
	ui->_tabBar->setFocusPolicy(Qt::NoFocus);
	assert_r(connect(ui->_tabBar, &QTabBar::currentChanged, this, &CPanelWidget::onTabBarCurrentChanged));
	assert_r(connect(ui->_tabBar, &QTabBar::tabCloseRequested, this, &CPanelWidget::onTabBarCloseRequested));
	assert_r(connect(ui->_tabBar, &QTabBar::tabMoved, this, &CPanelWidget::onTabBarTabMoved));
	assert_r(connect(ui->_tabBar, &QTabBar::customContextMenuRequested, this, &CPanelWidget::showContextMenuForTab));

	// Mirror however many tabs CController restored for this side (usually one, but persisted multi-tab state may have more).
	const std::vector<qulonglong> ids = _controller->tabIds(p);
	const qulonglong activeId = _controller->activeTabId(p);
	int activeIndex = 0;
	{
		const QSignalBlocker block(ui->_tabBar);
		for (const qulonglong id : ids)
		{
			PanelTab& tab = _tabs.emplace_back();
			populateTriplet(tab);

			const int index = ui->_tabBar->addTab(QString());
			ui->_tabBar->setTabData(index, id);
			if (id == activeId)
				activeIndex = index;
		}
		ui->_tabBar->setCurrentIndex(activeIndex);
	}
	for (int i = 0; i < (int)_tabs.size(); ++i)
		updateTabText(i);
	activateTab(activeIndex);
	updateTabBarVisibility();

	_controller->setPanelContentsChangedListener(p, this);
	_controller->setVolumesChangedListener(this);
	_controller->setCursorPositionListener(p, this);
}

void CPanelWidget::populateTriplet(PanelTab& tab)
{
	tab.model = new(std::nothrow) CFileListModel(_panelPosition, this);
	assert_r(connect(tab.model, &CFileListModel::itemEdited, this, &CPanelWidget::renameItem));

	tab.sortModel = new(std::nothrow) CFileListSortFilterProxyModel(this);
	tab.sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	tab.sortModel->setFilterRole(FullNameRole);
	tab.sortModel->setPanelPosition(_panelPosition);
	tab.sortModel->setSourceModel(tab.model);
	assert_r(connect(tab.sortModel, &QSortFilterProxyModel::modelAboutToBeReset, ui->_list, &CFileListView::modelAboutToBeReset));
	assert_r(connect(tab.sortModel, &CFileListSortFilterProxyModel::sorted, ui->_list, [this](){
		ui->_list->scrollTo(ui->_list->currentIndex());
	}));

	// A new tab starts sorted like the tab it was opened from (or Name/Ascending for the very first tab); from this point on each tab's sort is independent (see activateTab).
	if (_sortModel)
		tab.sortModel->sort(_sortModel->sortColumn(), _sortModel->sortOrder());
	else
		tab.sortModel->sort(NameColumn, Qt::AscendingOrder);

	// Header state only can stored the view has a model; during initPanel it doesn't yet (0 sections), and saving that
	// would later desync the 4-section header in activateTab. Leave it empty -> activateTab skips restoreState.
	if (ui->_list->header()->count() == NumberOfColumns)
		tab.headerState = ui->_list->header()->saveState();

	// Each tab owns its selection model (parented to the widget, not the view) so it survives the view->setModel() swaps.
	tab.selectionModel = new(std::nothrow) QItemSelectionModel(tab.sortModel, this);
	assert_r(connect(tab.selectionModel, &QItemSelectionModel::selectionChanged, this, &CPanelWidget::selectionChanged));
	assert_r(connect(tab.selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged));
}

void CPanelWidget::activateTab(int index)
{
	assert_and_return_r(index >= 0 && index < (int)_tabs.size(), );
	_activeTab = index;
	PanelTab& tab = _tabs[(size_t)index];
	_model = tab.model;
	_sortModel = tab.sortModel;
	_selectionModel = tab.selectionModel;

	// QTreeView::setModel(), with sortingEnabled on, unconditionally re-sorts whatever model it's just been
	// given using the header's CURRENT sort indicator -- a direct internal call made from inside setModel()
	// itself, not a signal, so a QSignalBlocker placed around/after the setModel() call can't stop it. Point
	// the indicator at the tab we're switching TO (its own remembered sort column/order) *before* the swap,
	// with signals blocked here so this step doesn't itself resort the OLD, still-attached model.
	QHeaderView* header = ui->_list->header();
	{
		const QSignalBlocker blocker(header);
		header->setSortIndicator(_sortModel->sortColumn(), _sortModel->sortOrder());
	}

	ui->_list->setModel(_sortModel); // re-sorts _sortModel by the indicator set above -- already its own sort, so this is a no-op
	ui->_list->setSelectionModel(_selectionModel);

	// Restore THIS tab's own column widths/order/visibility (captured by onTabBarCurrentChanged /
	// onTabBarCloseRequested when we last switched away from it), but keep the (now-correct) sort
	// indicator: restoring the stored blob's indicator here would just put this tab's OLD sort back,
	// the same way the plain setModel() call would if left unguarded.
	if (!tab.headerState.isEmpty())
	{
		const QSignalBlocker blocker(header);
		header->restoreState(tab.headerState);
		header->setSortIndicator(_sortModel->sortColumn(), _sortModel->sortOrder());
	}

	// Show the now-active panel's contents immediately (the CPanel may also refresh asynchronously on activation).
	fillFromPanel(_controller->panel(_panelPosition), refreshCauseOther);
}

void CPanelWidget::createNewTab()
{
	openPathInNewTab(_controller->panel(_panelPosition).currentDirPathPosix());
}

void CPanelWidget::openCurrentItemInNewTab()
{
	tryOpenItemInNewTab(_selectionModel->currentIndex(), /*activate=*/true);
}

void CPanelWidget::onItemMiddleClicked(const QModelIndex& sortModelIndex)
{
	// Middle-click opens a background tab (browser-style): don't switch the current panel away from what it's showing.
	tryOpenItemInNewTab(sortModelIndex, /*activate=*/false);
}

void CPanelWidget::tryOpenItemInNewTab(const QModelIndex& sortModelIndex, bool activate)
{
	const qulonglong hash = hashBySortModelIndex(sortModelIndex);
	if (hash == 0)
		return;

	const CFileSystemObject item = _controller->itemByHash(_panelPosition, hash);
	if (!item.isDir())
		return;

	openPathInNewTab(item.fullAbsolutePath(), activate); // For [..] this opens the parent folder ('..' is cleaned out of the path by QFileInfo)
}

void CPanelWidget::openPathInNewTab(const QString& path, bool activate)
{
	const qulonglong id = _controller->addTab(_panelPosition, path, activate);

	PanelTab& tab = _tabs.emplace_back();
	populateTriplet(tab);

	int index = 0;
	{
		const QSignalBlocker block(ui->_tabBar);
		index = ui->_tabBar->addTab(QString());
		ui->_tabBar->setTabData(index, id);
	}
	updateTabText(index);
	updateTabBarVisibility();

	if (activate)
		ui->_tabBar->setCurrentIndex(index); // fires currentChanged -> onTabBarCurrentChanged -> activateTab (controller side is already active, addTab(activate=true) did that)
}

void CPanelWidget::closeCurrentTab()
{
	onTabBarCloseRequested(_activeTab);
}

void CPanelWidget::duplicateCurrentTab()
{
	duplicateTab(_activeTab);
}

void CPanelWidget::closeAllTabsExceptCurrent()
{
	closeAllOtherTabs(_activeTab);
}

void CPanelWidget::reopenLastClosedTab()
{
	if (_recentlyClosedTabsPaths.empty())
		return;

	const QString path = _recentlyClosedTabsPaths.back();
	_recentlyClosedTabsPaths.pop_back();
	openPathInNewTab(path);
}

void CPanelWidget::switchToNextTab()
{
	if (_tabs.size() <= 1)
		return;
	ui->_tabBar->setCurrentIndex((_activeTab + 1) % (int)_tabs.size());
}

void CPanelWidget::switchToTabByPosition(int position)
{
	if (position < 0 || position >= (int)_tabs.size())
		return;
	ui->_tabBar->setCurrentIndex(position);
}

void CPanelWidget::switchToPreviousTab()
{
	if (_tabs.size() <= 1)
		return;
	ui->_tabBar->setCurrentIndex((_activeTab - 1 + (int)_tabs.size()) % (int)_tabs.size());
}

void CPanelWidget::onTabBarCurrentChanged(int index)
{
	if (index < 0 || index >= (int)_tabs.size())
		return;

	// Snapshot the tab we're switching away from so its column layout survives the swap; activateTab()
	// below restores the new tab's own snapshot.
	if (_activeTab >= 0 && _activeTab < (int)_tabs.size())
		_tabs[(size_t)_activeTab].headerState = ui->_list->header()->saveState();

	_controller->setActiveTab(_panelPosition, tabIdAt(index));
	activateTab(index);
}

void CPanelWidget::onTabBarCloseRequested(int index)
{
	if (index < 0 || index >= (int)_tabs.size())
		return;
	closeTabById(tabIdAt(index));
}

void CPanelWidget::closeTabById(qulonglong id)
{
	if (_tabs.size() <= 1)
		return;

	// Resolve id's CURRENT position fresh, right before acting on it: callers (e.g. closeAllOtherTabs) may
	// close several tabs in a row, and each earlier close shifts the positions of every tab after it.
	int index = -1;
	for (int i = 0; i < ui->_tabBar->count(); ++i)
	{
		if (tabIdAt(i) == id)
		{
			index = i;
			break;
		}
	}
	if (index < 0)
		return;

	// activateTab() runs unconditionally below, even when we're closing some OTHER tab and the active one
	// just shifts index -- without this, it would reapply the active tab's possibly-stale stored layout
	// over a live, not-yet-captured resize. Capture before the erase, while _activeTab still indexes the
	// correct (unshifted) slot. Closing the active tab itself (index == _activeTab) needs no capture, since
	// that tab is about to be destroyed anyway.
	if (index != _activeTab && _activeTab >= 0 && _activeTab < (int)_tabs.size())
		_tabs[(size_t)_activeTab].headerState = ui->_list->header()->saveState();

	_recentlyClosedTabsPaths.push_back(_controller->tabPath(_panelPosition, id));

	_controller->closeTab(_panelPosition, id);
	const qulonglong activeId = _controller->activeTabId(_panelPosition); // post-removal active tab

	// Detach the view from the closing triplet (via activateTab below) BEFORE deleting it.
	const PanelTab closing = _tabs[(size_t)index];
	_tabs.erase(_tabs.begin() + index);
	int controllerActiveIndex = 0;
	{
		const QSignalBlocker block(ui->_tabBar);
		ui->_tabBar->removeTab(index);
		// Removing 'index' shifted every later tab's position; find wherever the active tab landed by id
		// rather than assuming any particular arithmetic on 'index'.
		for (int i = 0; i < ui->_tabBar->count(); ++i)
		{
			if (tabIdAt(i) == activeId)
			{
				controllerActiveIndex = i;
				break;
			}
		}
		ui->_tabBar->setCurrentIndex(controllerActiveIndex);
	}
	updateTabBarVisibility();
	activateTab(controllerActiveIndex);

	delete closing.selectionModel;
	delete closing.sortModel;
	delete closing.model;
}

void CPanelWidget::onTabBarTabMoved(int from, int to)
{
	if (from == to)
		return;

	// By the time this signal fires, the QTabBar (and the per-tab data carrying its tab ID) has already moved
	// the tab internally -- mirror the same reorder into _tabs, which has no notion of its own beyond position.
	PanelTab movedTab = std::move(_tabs[(size_t)from]);
	_tabs.erase(_tabs.begin() + from);
	_tabs.insert(_tabs.begin() + to, std::move(movedTab));

	if (_activeTab == from)
		_activeTab = to;
	else if (from < _activeTab && to >= _activeTab)
		--_activeTab; // the active tab shifted left to fill the gap the move left behind
	else if (from > _activeTab && to <= _activeTab)
		++_activeTab; // the active tab shifted right to make room for the incoming tab

	_controller->moveTabPosition(_panelPosition, tabIdAt(to), (size_t)to);
}

void CPanelWidget::showContextMenuForTab(QPoint pos)
{
	const int index = ui->_tabBar->tabAt(pos);
	if (index < 0)
		return;

	QMenu menu;
	QAction* duplicateTabAction = menu.addAction(tr("Duplicate tab"));
	QAction* closeTabAction = menu.addAction(tr("Close tab") + QSL("\tMMB")); // The part after '\t' renders in the shortcut column (display only)
	closeTabAction->setEnabled(_tabs.size() > 1);
	QAction* closeOthersAction = menu.addAction(tr("Close all other tabs"));
	closeOthersAction->setEnabled(_tabs.size() > 1);

	const QAction* chosenAction = menu.exec(ui->_tabBar->mapToGlobal(pos));
	if (chosenAction == duplicateTabAction)
		duplicateTab(index);
	else if (chosenAction == closeTabAction)
		onTabBarCloseRequested(index);
	else if (chosenAction == closeOthersAction)
		closeAllOtherTabs(index);
}

void CPanelWidget::duplicateTab(int index)
{
	// Browser-style background tab, like middle-click: don't switch away from what's currently showing.
	openPathInNewTab(_controller->tabPath(_panelPosition, tabIdAt(index)), /*activate=*/false);
}

void CPanelWidget::closeAllOtherTabs(int index)
{
	assert_and_return_r(index >= 0 && index < (int)_tabs.size(), );

	const qulonglong keepId = tabIdAt(index);
	std::vector<qulonglong> idsToClose;
	for (int i = 0; i < ui->_tabBar->count(); ++i)
	{
		const qulonglong id = tabIdAt(i);
		if (id != keepId)
			idsToClose.push_back(id);
	}

	for (const qulonglong id : idsToClose)
		closeTabById(id);
}

void CPanelWidget::updateTabBarVisibility()
{
	ui->_tabBar->setVisible(_tabs.size() > 1); // "no tabs created by the user" => no tab bar in the UI
}

void CPanelWidget::updateTabText(int index)
{
	if (index >= 0 && index < ui->_tabBar->count())
		ui->_tabBar->setTabText(index, _controller->tabName(_panelPosition, tabIdAt(index)));
}

QString CPanelWidget::tabToolTipText(int index) const
{
	const CPanel& tab = _controller->tabById(_panelPosition, tabIdAt(index));
	const FolderContentsSummary contents = summarizeFolderContents(tab.list());

	return tab.currentDirPathNative() % '\n' %
		tr("%1 folders, %2 files (%3)").arg(contents.numFolders).arg(contents.numFiles).arg(fileSizeToString(contents.size));
}

qulonglong CPanelWidget::tabIdAt(int index) const
{
	return ui->_tabBar->tabData(index).toULongLong();
}

// Returns the list of items added to the view
void CPanelWidget::fillFromList(FileListRefreshCause operation)
{
	CTimeElapsed timer{ true };

	disconnect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged);

	const QString previousFolder = _directoryCurrentlyBeingDisplayed;
	const QModelIndex previousCurrentIndex = _selectionModel->currentIndex();

	_model->onPanelContentsChanged(_controller->panel(_panelPosition).itemHashes());

	auto indexUnderCursor = _sortModel->index(0, 0);

	// Setting the cursor position as appropriate
	if (operation == refreshCauseCdUp)
	{
		const auto previousFolderHash = CFileSystemObject{ previousFolder }.hash();
		if (const auto index = indexByHash(previousFolderHash); index.isValid())
			indexUnderCursor = index;
	}
	else if (operation != refreshCauseForwardNavigation || CSettings().value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS).toBool())
	{
		const qulonglong itemHashToSetCursorTo = _controller->currentItemHashForFolder(_panelPosition, _controller->panel(_panelPosition).currentDirPathPosix());
		const QModelIndex itemIndexToSetCursorTo = indexByHash(itemHashToSetCursorTo, true);
		if (itemIndexToSetCursorTo.isValid())
			indexUnderCursor = itemIndexToSetCursorTo;
		else if (previousCurrentIndex.isValid() && operation != refreshCauseCdUp && operation != refreshCauseForwardNavigation)
			indexUnderCursor = _sortModel->index(std::min(previousCurrentIndex.row(), _sortModel->rowCount() - 1), 0);
	}

	ui->_list->moveCursorToItem(indexUnderCursor);

	assert_r(connect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged));
	currentItemChanged(_selectionModel->currentIndex(), QModelIndex());
	selectionChanged(QItemSelection(), QItemSelection());

	if (_model->rowCount() > 1000)
		qInfo() << __FUNCTION__ << "Procesing" << _model->rowCount() << "items took" << timer.elapsed() << "ms";
}

void CPanelWidget::fillFromPanel(const CPanel &panel, FileListRefreshCause operation)
{
	const auto previousSelection = selectedItemsHashes(true);
	// Mapping hash -> full path, so that a hash collision against a different, unrelated file that appears after the refresh can't cause it to be silently re-selected in place of the item the user actually had selected.
	ankerl::unordered_dense::segmented_map<qulonglong, QString, IdentityHash> selectedItemsHashes;
	for (const auto selectedItemHash: previousSelection)
		selectedItemsHashes[selectedItemHash] = _controller->itemByHash(_panelPosition, selectedItemHash).fullAbsolutePath();

	fillFromList(operation);
	_directoryCurrentlyBeingDisplayed = panel.currentDirPathPosix();

	// Restoring previous selection
	if (!selectedItemsHashes.empty())
	{
		CTimeElapsed timer(true);
		QItemSelection selection;
		for (int row = 0, numRows = _sortModel->rowCount(); row < numRows; ++row)
		{
			const QModelIndex idx = _sortModel->index(row, 0);
			const qulonglong hash = hashBySortModelIndex(idx);
			const auto it = selectedItemsHashes.find(hash);
			if (it != selectedItemsHashes.end() && _controller->itemByHash(_panelPosition, hash).fullAbsolutePath() == it->second)
				selection.select(idx, idx);
		}

		timer.start();
		if (!selection.empty())
			_selectionModel->select(selection, QItemSelectionModel::Rows | QItemSelectionModel::Select);

		if (const auto elapsedMs = timer.elapsed(); elapsedMs >= 100)
			qInfo() << "_selectionModel->select took" << elapsedMs << "ms for" << selection.size() << "items";
	}

	fillHistory();
	updateCurrentVolumeButtonAndInfoLabel();
}

void CPanelWidget::showContextMenuForItems(QPoint pos)
{
	const auto selection = selectedItemsHashes(true);
	std::vector<std::wstring> paths;
	if (selection.empty())
		paths.push_back(_controller->panel(_panelPosition).currentDirPathNative().toStdWString());
	else
	{
		for (size_t i = 0; i < selection.size(); ++i)
		{
			if (!_controller->itemByHash(_panelPosition, selection[i]).isCdUp() || selection.size() == 1)
			{
				QString selectedItemPath = _controller->itemPath(_panelPosition, selection[i]);
				paths.push_back(selectedItemPath.toStdWString());
			}
			else if (!selection.empty())
			{
				// This is a cdup element ([..]), and we should remove selection from it
				_selectionModel->select(indexByHash(selection[i]), QItemSelectionModel::Clear | QItemSelectionModel::Rows);
			}
		}
	}

	pos *= ui->_list->devicePixelRatioF();
	OsShell::openShellContextMenuForObjects(paths, pos.x(), pos.y(), reinterpret_cast<void*>(winId()));
}

void CPanelWidget::showContextMenuForDisk(QPoint pos)
{
#ifdef _WIN32
	const auto* button = dynamic_cast<const QPushButton*>(sender());
	if (!button)
		return;

	pos = button->mapToGlobal(pos) * button->devicePixelRatioF(); // These coordinates ar egoing directly into the system API so need to account for scaling that Qt tries to abstract away.
	const uint64_t diskId = button->property("id").toULongLong();
	const auto volumeInfo = _controller->volumeInfoById(diskId);
	assert_and_return_r(volumeInfo, );
	std::vector<std::wstring> diskPath(1, volumeInfo->rootObjectInfo.fullAbsolutePath().toStdWString());
	OsShell::openShellContextMenuForObjects(diskPath, pos.x(), pos.y(), reinterpret_cast<HWND>(winId()));
#else
	Q_UNUSED(pos);
#endif
}

void CPanelWidget::onSpacePressed()
{
	const QModelIndex itemIndex = _selectionModel->currentIndex();
	if (itemIndex.isValid())
	{
		_selectionModel->select(itemIndex, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
		_controller->displayDirSize(_panelPosition, hashBySortModelIndex(itemIndex));
	}
}

void CPanelWidget::invertCurrentItemSelection()
{
	const QAbstractItemModel * model = _selectionModel->model();
	QModelIndex item = _selectionModel->currentIndex();
	QModelIndex next = model->index(item.row() + 1, 0);
	if (item.isValid())
		_selectionModel->select(item, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
	if (next.isValid())
		ui->_list->moveCursorToItem(next);
}

void CPanelWidget::driveButtonClicked()
{
	if (!sender())
		return;

	const auto id = sender()->property("id").toULongLong();
	if (const auto result = _controller->switchToVolume(_panelPosition, id); !result.first)
		QMessageBox::information(this, tr("Failed to switch volume"), tr("The volume %1 is inaccessible (locked or doesn't exist).").arg(result.second));

	ui->_list->setFocus();
}

void CPanelWidget::selectionChanged(const QItemSelection& selected, const QItemSelection& /*deselected*/)
{
	// This doesn't let the user select the [..] item

	const QString cdUpPath = CFileSystemObject(currentDirPathNative()).parentDirPath();
	for (auto&& indexRange: selected)
	{
		auto indexList = indexRange.indexes();
		for (const auto& index: indexList)
		{
			const auto hash = hashBySortModelIndex(index);
			if (_controller->itemByHash(_panelPosition, hash).fullAbsolutePath() == cdUpPath)
			{
				auto cdUpIndex = indexByHash(hash);
				assert_r(cdUpIndex.isValid());
				_selectionModel->select(cdUpIndex, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
				break;
			}
		}
	}

	const auto selection = selectedItemsHashes();
	// Updating the selection summary label
	updateInfoLabel(selection);

	// Notify the controller of the new selection
	CPluginEngine::get().selectionChanged(_panelPosition, selection);
}

void CPanelWidget::currentItemChanged(const QModelIndex& current, const QModelIndex& /*previous*/)
{
	const qulonglong hash = current.isValid() ? hashBySortModelIndex(current) : 0;
	_controller->setCursorPositionForCurrentFolder(_panelPosition, hash, false);

	emit currentItemChangedSignal(_panelPosition, hash);
}

void CPanelWidget::setCursorToItem(const QString& folder, qulonglong currentItemHash)
{
	if (ui->_list->editingInProgress())
		return; // Can't move cursor while editing is in progress, it crashes inside Qt

	if (_controller->panel(_panelPosition).currentDirObject().fullAbsolutePath() != folder)
		return;

	const auto newCurrentIndex = indexByHash(currentItemHash);
	if(newCurrentIndex.isValid())
		_selectionModel->setCurrentIndex(newCurrentIndex, QItemSelectionModel::Current | QItemSelectionModel::Rows);
}

void CPanelWidget::renameItem(qulonglong hash, QString newName)
{
	const CFileSystemObject item = _controller->itemByHash(_panelPosition, hash);
	if (item.isCdUp())
		return;

	const QString parentPath = item.parentDirPath();
	assert_r(parentPath.endsWith('/'));
	newName = FileSystemHelpers::trimUnsupportedSymbols(newName);

	const auto source = parseOperationPath(item.fullAbsolutePath());
	if (!source)
		return; // A real panel item always has a valid absolute path

	auto result = inlineRename(*source, newName, false);
	if (result.status == InlineRenameStatus::ReplacementRequired)
	{
		// Fix for https://github.com/VioletGiraffe/file-commander/issues/123
		if (QMessageBox::question(this, tr("Item already exists"),
				tr("The file %1 already exists, do you want to overwrite it?").arg(newName),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
			return;

		result = inlineRename(*source, newName, true);
	}

	switch (result.status)
	{
	case InlineRenameStatus::Renamed:
	case InlineRenameStatus::NothingToDo:
		// Move the cursor to the renamed item.
		_controller->setCursorPositionForCurrentFolder(_panelPosition, CFileSystemObject(parentPath + newName).hash());
		break;

	case InlineRenameStatus::Rejected:
		QMessageBox::information(this, tr("Item already exists"),
			tr("Cannot rename this %1 to \"%2\": a %3 with that name already exists.")
				.arg(fileOperationEntryKindNoun(*result.sourceKind), newName, fileOperationEntryKindNoun(*result.destinationKind)));
		break;

	case InlineRenameStatus::RejectedInvalidName:
		QMessageBox::warning(this, tr("Invalid name"), tr("\"%1\" is not a valid file or folder name.").arg(newName));
		break;

	case InlineRenameStatus::Failed:
	{
		QString message = tr("Failed to rename %1 to %2").arg(item.fullName(), newName);
		if (result.failure)
			message.append(":\n" % fileSystemErrorText(result.failure->filesystemError) % '.');
		QMessageBox::critical(this, tr("Renaming failed"), message);
		break;
	}

	case InlineRenameStatus::ReplacementRequired:
		break; // Declined above
	}
}

void CPanelWidget::toRoot()
{
	if (!_currentVoumePath.isEmpty())
		_controller->setPath(_panelPosition, _currentVoumePath, refreshCauseOther);
}

void CPanelWidget::showFavoriteLocationsMenu(QPoint pos)
{
	QMenu menu;
	std::function<void(QMenu *, std::vector<CLocationsCollection>&)> createMenus = [this, &createMenus](QMenu * parentMenu, std::vector<CLocationsCollection>& locations)
	{
		for (auto& item: locations)
		{
			if (item.subLocations.empty() && !item.absolutePath.isEmpty())
			{
				QAction * action = parentMenu->addAction(item.displayName);
				const QString& path = toPosixSeparators(item.absolutePath);
				if (CFileSystemObject(path) == CFileSystemObject(currentDirPathNative()))
				{
					action->setCheckable(true);
					action->setChecked(true);
				}

				assert_r(connect(action, &QAction::triggered, this, [this, path](){
					_controller->setPath(_panelPosition, path, refreshCauseOther);
				}));
			}
			else
			{
				QMenu * subMenu = parentMenu->addMenu(item.displayName);
				createMenus(subMenu, item.subLocations);
			}
		}

		if (!locations.empty())
			parentMenu->addSeparator();

		QAction * addFolderAction = parentMenu->addAction(tr("Add current folder here..."));
		assert_r(QObject::connect(addFolderAction, &QAction::triggered, this, [this, &locations](){
			const QString path = currentDirPathNative();
			const QString displayName = CFileSystemObject(path).name();
			const QString name = QInputDialog::getText(this, tr("Enter the name"), tr("Enter the name to store the current location under"), QLineEdit::Normal, displayName.isEmpty() ? path : displayName);
			if (!name.isEmpty() && !path.isEmpty())
			{
				if (std::find_if(locations.cbegin(), locations.cend(), [&path](const CLocationsCollection& entry){return entry.absolutePath == path;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("This item already exists here (possibly under a different name)."), QMessageBox::Cancel);
					return;
				}
				else if (std::find_if(locations.cbegin(), locations.cend(), [&name](const CLocationsCollection& entry){return entry.displayName == name;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("And item with the same name already exists here (possibly pointing to a different location)."), QMessageBox::Cancel);
					return;
				}

				_controller->favoriteLocations().addItem(locations, name, currentDirPathNative());
			}
		}));

		QAction * addCategoryAction = parentMenu->addAction(tr("Add a new subcategory..."));
		assert_r(QObject::connect(addCategoryAction, &QAction::triggered, this, [this, &locations, parentMenu](){
			const QString name = QInputDialog::getText(this, tr("Enter the name"), tr("Enter the name for the new subcategory"));
			if (!name.isEmpty())
			{
				if (std::find_if(locations.cbegin(), locations.cend(), [&name](const CLocationsCollection& entry){return entry.displayName == name;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("An item with the same name already exists here (possibly pointing to a different location)."), QMessageBox::Cancel);
					return;
				}

				parentMenu->addMenu(name);
				_controller->favoriteLocations().addItem(locations, name);
			}
		}));
	};

	createMenus(&menu, _controller->favoriteLocations().locations());
	menu.addSeparator();
	QAction * edit = menu.addAction(tr("Edit..."));
	assert_r(connect(edit, &QAction::triggered, this, &CPanelWidget::showFavoriteLocationsEditor));
	const QAction* action = menu.exec(pos);
	if (action) // Something was selected
		setFocusToFileList(); // #84
}

void CPanelWidget::showFavoriteLocationsEditor()
{
	CFavoriteLocationsEditor(this).exec();
}

void CPanelWidget::fileListViewKeyPressed(const QString& /*keyText*/, int key, Qt::KeyboardModifiers /*modifiers*/)
{
	if (key == Qt::Key_Backspace)
	{
		// Navigating back
		_controller->navigateUp(_panelPosition);
	}
}

void CPanelWidget::showFilterEditor()
{
	_filterDialog->showAt(QPoint{ 0, ui->_list->height() });
	filterTextEdited(_filterDialog->text());
}

void CPanelWidget::filterTextEdited(const QString& filterText)
{
	if (_sortModel->rowCount() < 1000)
		_sortModel->setFilterWildcard(filterText);
}

void CPanelWidget::filterTextConfirmed(const QString& filterText)
{
	_sortModel->setFilterWildcard(filterText);
	raise();
	ui->_list->setFocus();
}

void CPanelWidget::copySelectionToClipboard() const
{
#ifndef _WIN32
	const QModelIndexList selection(_selectionModel->selectedRows());
	QModelIndexList mappedIndexes;
	for (const auto& index: selection)
		mappedIndexes.push_back(_sortModel->mapToSource(index));

	if (mappedIndexes.empty())
	{
		auto currentIndex = _selectionModel->currentIndex();
		if (currentIndex.isValid())
			mappedIndexes.push_back(_sortModel->mapToSource(currentIndex));
	}

	QClipboard * clipBoard = QApplication::clipboard();
	if (clipBoard)
	{
		QMimeData * data = _model->mimeData(mappedIndexes);
		if (data)
			data->setProperty("cut", false);
		clipBoard->setMimeData(data);
	}
#else
	const auto hashes = selectedItemsHashes();
	std::vector<std::wstring> paths;
	paths.reserve(hashes.size());
	for (auto hash: hashes)
		paths.emplace_back(_controller->itemByHash(_panelPosition, hash).fullAbsolutePath().toStdWString());

	OsShell::copyObjectsToClipboard(paths, reinterpret_cast<void*>(winId()));
#endif
}

void CPanelWidget::cutSelectionToClipboard() const
{
#ifndef _WIN32
	const QModelIndexList selection(_selectionModel->selectedRows());
	QModelIndexList mappedIndexes;
	for (const auto& index: selection)
		mappedIndexes.push_back(_sortModel->mapToSource(index));

	if (mappedIndexes.empty())
	{
		auto currentIndex = _selectionModel->currentIndex();
		if (currentIndex.isValid())
			mappedIndexes.push_back(_sortModel->mapToSource(currentIndex));
	}

	QClipboard * clipBoard = QApplication::clipboard();
	if (clipBoard)
	{
		QMimeData * data = _model->mimeData(mappedIndexes);
		if (data)
			data->setProperty("cut", true);
		clipBoard->setMimeData(data);
	}
#else
	std::vector<std::wstring> paths;
	auto hashes = selectedItemsHashes();
	paths.reserve(hashes.size());
	for (auto hash: hashes)
		paths.emplace_back(_controller->itemByHash(_panelPosition, hash).fullAbsolutePath().toStdWString());

	OsShell::cutObjectsToClipboard(paths, reinterpret_cast<void*>(winId()));
#endif
}

void CPanelWidget::pasteSelectionFromClipboard(bool specialPaste)
{
	QClipboard * clipBoard = QApplication::clipboard();
	// If the clipboard contains an image (not a file), paste it into a file
	if (clipBoard && clipBoard->mimeData()->hasImage())
	{
		QImage image = qvariant_cast<QImage>(clipBoard->mimeData()->imageData());
		assert_r(pasteImage(image, specialPaste));
		return;
	}

#ifndef _WIN32
	if (clipBoard)
	{
		const QMimeData * data = clipBoard->mimeData();
		_model->dropMimeData(clipBoard->mimeData(), (data && data->property("cut").toBool()) ? Qt::MoveAction : Qt::CopyAction, 0, 0, QModelIndex());
	}
#else
	auto* hwnd = reinterpret_cast<void*>(winId());
	const auto currentDirWString = currentDirPathNative().toStdWString();
	_controller->execOnWorkerThread([hwnd, currentDirWString]() {
		OsShell::pasteFilesAndFoldersFromClipboard(currentDirWString, hwnd);
	});
#endif
}

void CPanelWidget::pathFromHistoryActivated(QString path)
{
	path.remove('\"');

	const CFileSystemObject processedPath(path); // Needed for expanding environment variables in the path
	ui->_list->setFocus();
	if (_controller->setPath(_panelPosition, processedPath.fullAbsolutePath(), refreshCauseOther) == FileOperationResultCode::DirNotAccessible)
		QMessageBox::information(this, tr("Failed to set the path"), tr("The path %1 is inaccessible (locked or doesn't exist). Setting the closest accessible path instead.").arg(path));
}

void CPanelWidget::fillHistory()
{
	const auto& visited = _controller->visitedLocations(_panelPosition);
	if (visited.empty())
		return;

	ui->_pathNavigator->clear();

	static const auto ensureTrailingSlash = [](const QString& path) {
		return path.endsWith('/') ? path : path + '/';
	};

	QStringList items;
	items.reserve((qsizetype)visited.size());
	std::unordered_set<QString> seen;
	for (auto it = visited.rbegin(); it != visited.rend(); ++it)
	{
		items.push_back(toNativeSeparators(ensureTrailingSlash(*it)));
		seen.insert(*it);
	}

	// The other panel's visited folders, appended after this panel's own, deduplicated, added to the bottom without displacing this panel's own most-recent entries from the top.
	const auto& otherVisited = _controller->visitedLocations(CController::otherPanelPosition(_panelPosition));
	for (auto it = otherVisited.rbegin(); it != otherVisited.rend(); ++it)
	{
		if (seen.insert(*it).second)
			items.push_back(toNativeSeparators(ensureTrailingSlash(*it)));
	}

	// The active tab's current directory might not be in either list yet (e.g. a deferred save hasn't
	// caught up) -- prepend it so it's still shown/highlighted correctly instead of defaulting to some
	// unrelated entry.
	const QString currentDir = _controller->panel(_panelPosition).currentDirPathPosix();
	const QString currentDirDisplay = toNativeSeparators(ensureTrailingSlash(currentDir));
	qsizetype currentDirRow = items.indexOf(currentDirDisplay);
	if (currentDirRow < 0)
	{
		items.push_front(currentDirDisplay);
		currentDirRow = 0;
	}

	ui->_pathNavigator->addItems(items);
	ui->_pathNavigator->setCurrentIndex((int)currentDirRow);
}

void CPanelWidget::updateInfoLabel(const std::vector<qulonglong>& selection)
{
	const FolderContentsSummary total = summarizeFolderContents(_controller->panel(_panelPosition).list());

	uint64_t numFilesSelected = 0;
	uint64_t numFoldersSelected = 0;
	uint64_t sizeSelected = 0;

	for (const auto selectedItem: selection)
	{
		const CFileSystemObject object = _controller->itemByHash(_panelPosition, selectedItem);
		if (object.isCdUp())
			continue;

		if (object.isFile())
			++numFilesSelected;
		else if (object.isDir())
			++numFoldersSelected;

		sizeSelected += object.size();
	}

	ui->_infoLabel->setText(tr("%1/%2 files, %3/%4 folders selected (%5 / %6)").arg(numFilesSelected).arg(total.numFiles).
		arg(numFoldersSelected).arg(total.numFolders).
		arg(fileSizeToString(sizeSelected), fileSizeToString(total.size)));
}

bool CPanelWidget::fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item)
{
	assert_and_return_r(item.isValid(), false);
	const QModelIndex source = _sortModel->mapToSource(item);
	const qulonglong hash = _model->itemHash(source);
	emit itemActivated(hash, this);
	return true; // Consuming the event
}

void CPanelWidget::volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept
{
	if (p != _panelPosition)
		return;

	_currentVoumePath.clear();

	if (!ui->_driveButtonsWidget->layout())
	{
#ifdef _WIN32
		auto* flowLayout = new CFlowLayout(ui->_driveButtonsWidget, 0, 0, 0);
#else
		auto* flowLayout = new CFlowLayout(ui->_driveButtonsWidget, 0, 5, 5);
#endif
		flowLayout->setSpacing(1);
		ui->_driveButtonsWidget->setLayout(flowLayout);
	}

	if (drivesListOrReadinessChanged)
	{
		// Clearing and deleting the previous buttons
		QLayout* layout = ui->_driveButtonsWidget->layout();
		assert_r(layout);
		while (layout->count() > 0)
		{
			QLayoutItem* item = layout->takeAt(0);
			item->widget()->deleteLater();
			delete item;
		}

		// Creating and adding new buttons
		for (const VolumeInfo& volume: drives)
		{
			if (!volume.isReady || !volume.rootObjectInfo.isValid())
				continue;

#ifdef _WIN32
			const QString name = volume.rootObjectInfo.fullAbsolutePath().remove(QL1(":/"));
#else
			const QString name = volume.volumeLabel;
#endif

			assert_r(layout);
			auto* diskButton = new(std::nothrow) QPushButton;
			diskButton->setFocusPolicy(Qt::NoFocus);
			diskButton->setCheckable(true);
			diskButton->setIcon(CIconProvider::iconForFilesystemObject(volume.rootObjectInfo, false));
			diskButton->setText(name);
			diskButton->setFixedWidth(QFontMetrics{ diskButton->font() }.horizontalAdvance(diskButton->text()) + 5 + diskButton->iconSize().width() + 20);
			diskButton->setProperty("id", (qulonglong)volume.id());
			diskButton->setContextMenuPolicy(Qt::CustomContextMenu);
			assert_r(connect(diskButton, &QPushButton::clicked, this, &CPanelWidget::driveButtonClicked));
			assert_r(connect(diskButton, &QPushButton::customContextMenuRequested, this, &CPanelWidget::showContextMenuForDisk));
			layout->addWidget(diskButton);
		}
	}

	updateCurrentVolumeButtonAndInfoLabel();
}

qulonglong CPanelWidget::hashBySortModelIndex(const QModelIndex &index) const
{
	if (!index.isValid())
		return 0;

	const auto sourceIndex = _sortModel->mapToSource(index);
	const auto hash =_model->itemHash(sourceIndex); // Could only be 0 if some kind of desync occurred between the UI view and the internal data of the model
	assert_debug_only(hash != 0);
	return hash;
}

QModelIndex CPanelWidget::indexByHash(const qulonglong hash, bool logFailures) const
{
	if (hash == 0)
		return {};

	for(int row = 0, numRows = _sortModel->rowCount(); row < numRows; ++row)
	{
		const auto index = _sortModel->index(row, 0);
		if (hashBySortModelIndex(index) == hash)
			return index;
	}

	if (logFailures)
		qInfo() << "Failed to find hash" << hash << "in" << currentDirPathNative();

	return {};
}

bool CPanelWidget::eventFilter(QObject * object, QEvent * e)
{
	if (object == ui->_list && e->type() == QEvent::ContextMenu)
	{
		showContextMenuForItems(QCursor::pos()); // QCursor::pos() returns global pos
		return true;
	}
	else if(object == ui->_list->viewport() && e->type() == QEvent::Wheel)
	{
		auto* wEvent = static_cast<QWheelEvent*>(e);
		if (wEvent && wEvent->modifiers() == Qt::ShiftModifier)
		{
			if (wEvent->angleDelta().y() > 0)
				_controller->navigateBack(_panelPosition);
			else
				_controller->navigateForward(_panelPosition);
			return true;
		}
	}
	else if (object == ui->_list->viewport() && e->type() == QEvent::MouseButtonPress)
	{
		const auto* mouseEvent = static_cast<QMouseEvent*>(e);
		if (mouseEvent->button() == Qt::BackButton)
		{
			_controller->navigateBack(_panelPosition);
			return true;
		}
		else if (mouseEvent->button() == Qt::ForwardButton)
		{
			_controller->navigateForward(_panelPosition);
			return true;
		}
	}
	else if (object == ui->_tabBar && e->type() == QEvent::MouseButtonRelease)
	{
		// Release, not press: don't close on an accidental press-and-drag-away, matching browser behavior
		const auto* mouseEvent = static_cast<QMouseEvent*>(e);
		if (mouseEvent->button() == Qt::MiddleButton)
		{
			const int index = ui->_tabBar->tabAt(mouseEvent->pos());
			if (index >= 0)
				onTabBarCloseRequested(index);
			return true;
		}
	}
	else if (object == ui->_tabBar && e->type() == QEvent::ToolTip)
	{
		// Composed on demand rather than set via setTabToolTip so that the stats are never stale
		const auto* helpEvent = static_cast<QHelpEvent*>(e);
		const int index = ui->_tabBar->tabAt(helpEvent->pos());
		if (index >= 0)
			QToolTip::showText(helpEvent->globalPos(), tabToolTipText(index), ui->_tabBar);
		else
			QToolTip::hideText();
		return true;
	}
	else if (object == ui->_pathNavigator && e->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(e);
		if (keyEvent->key() == Qt::Key_Escape)
		{
			ui->_pathNavigator->resetToLastSelected(false);
			ui->_list->setFocus();
		}
	}

	return QWidget::eventFilter(object, e);
}

void CPanelWidget::onPanelContentsChanged(Panel p , FileListRefreshCause operation)
{
	if (p == _panelPosition)
	{
		fillFromPanel(_controller->panel(_panelPosition), operation);
		updateTabText(_activeTab);
	}
}

CFileListView *CPanelWidget::fileListView() const
{
	return ui->_list;
}

QAbstractItemModel * CPanelWidget::model() const
{
	return _model;
}

QSortFilterProxyModel *CPanelWidget::sortModel() const
{
	return _sortModel;
}

std::vector<qulonglong> CPanelWidget::selectedItemsHashes(bool onlyHighlightedItems /* = false */) const
{
	auto selection = _selectionModel->selectedRows();
	std::vector<qulonglong> result;

	if (!selection.empty())
	{
		result.reserve(selection.size());
		for (const auto& selectedItem: selection)
		{
			const qulonglong hash = hashBySortModelIndex(selectedItem);
			if (!_controller->itemByHash(_panelPosition, hash).isCdUp())
				result.push_back(hash);
		}
	}
	else if (!onlyHighlightedItems)
	{
		auto currentIndex = _selectionModel->currentIndex();
		if (currentIndex.isValid())
		{
			const auto hash = hashBySortModelIndex(currentIndex);
			if (!_controller->itemByHash(_panelPosition, hash).isCdUp())
				result.push_back(hash);
		}
	}

	return result;
}

qulonglong CPanelWidget::currentItemHash() const
{
	const QModelIndex currentIndex = _selectionModel->currentIndex();
	return hashBySortModelIndex(currentIndex);
}

void CPanelWidget::invertSelection()
{
	ui->_list->invertSelection();
}

void CPanelWidget::moveCursorToFirstFile()
{
	const int row = _sortModel->firstFileRow();
	if (row >= 0)
		ui->_list->moveCursorToItem(_sortModel->index(row, 0));
}

void CPanelWidget::copySelectedItemsPathsToClipboard() const
{
	auto selection = _selectionModel->selectedRows();
	std::sort(selection.begin(), selection.end(), [](const QModelIndex& l, const QModelIndex& r) { return l.row() < r.row(); });

	QString paths;
	for (const auto& index : selection)
	{
		const CFileSystemObject item = _controller->itemByHash(_panelPosition, hashBySortModelIndex(index));
		if (!item.isCdUp())
			paths += escapedPath(toNativeSeparators(item.fullAbsolutePath())) + '\n';
	}

	if (!paths.isEmpty())
	{
		paths.chop(1);
		QApplication::clipboard()->setText(paths);
	}
	else // Nothing (or only [..]) selected
		_controller->copyCurrentItemPathToClipboard();
}

void CPanelWidget::onSettingsChanged()
{
	QFont font;
	if (font.fromString(CSettings{}.value(KEY_INTERFACE_FILE_LIST_FONT, INTERFACE_FILE_LIST_FONT_DEFAULT).toString()))
		ui->_list->setFont(font);
}

void CPanelWidget::updateCurrentVolumeButtonAndInfoLabel()
{
	const auto currentVolumeInfo = _controller->currentVolumeInfo(_panelPosition);
	if (currentVolumeInfo)
	{
		_currentVoumePath = currentVolumeInfo->rootObjectInfo.fullAbsolutePath();
		const QString volumeInfoText = tr("%1 (%2): <b>%4 free</b> of %5 total").
									   arg(currentVolumeInfo->volumeLabel, currentVolumeInfo->fileSystemName, fileSizeToString(currentVolumeInfo->freeSize, 'M', QSL(" ")), fileSizeToString(currentVolumeInfo->volumeSize, 'M', QSL(" ")));
		ui->_driveInfoLabel->setText(volumeInfoText);
	}
	else
		ui->_driveInfoLabel->clear();

	auto* layout = ui->_driveButtonsWidget->layout();
	if (!layout)
		return; // The drive buttons (and their layout) aren't created until volumesChanged() first runs; nothing to update yet.

	for (int i = 0, n = layout->count(); i < n; ++i)
	{
		auto* button = dynamic_cast<QPushButton*>(layout->itemAt(i)->widget());
		if (!button)
			continue;

		const auto id = button->property("id").toULongLong();
		button->setChecked(currentVolumeInfo ? (id == currentVolumeInfo->id()) : false); // Need to clear all selection if the current volume cannot be determined
	}
}

bool CPanelWidget::pasteImage(const QImage& image, bool lossyCompression)
{
	const QString currentDirPath = currentDirPathNative();
	assert(currentDirPath.endsWith(nativeSeparator()));

	const QString imagePathTemplate = currentDirPath + (lossyCompression ? "%1.jpg" : "%1.png");
	QString imagePath = imagePathTemplate.arg("clipboard");
	for (int i = 1; QFile::exists(imagePath); ++i)
	{
		imagePath = imagePathTemplate.arg("clipboard_" + QString::number(i));
	}

	return lossyCompression ? image.save(imagePath, "jpg", 75) : image.save(imagePath, "png");
}

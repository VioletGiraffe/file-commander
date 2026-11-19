#include "cpanelwidget.h"

#include "cbreadcrumbbar.h"
#include "cdrivebar.h"
#include "columns.h"
#include "cvolumeusagebar.h"
#include "fileclipboard.h"
#include "filelistwidget/cfilelistfilterdialog.h"
#include "filelistwidget/model/cfilelistmodel.h"
#include "tabviewstates.h"

#include "cmainwindow.h"
#include "themes/theme.h"

#include "favoritelocationseditor/favoritelocationsmenu.h"

#include "settings.h"
#include "shell/cshell.h"
#include "filelist/filetimetext.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "directoryscan/thinioqstring.h"
#include "fileoperations/fileoperationtexts.h"
#include "fileoperations/fileoperationtypes.h"
#include "fileoperations/inlinerename.h"
#include "fileoperationsui/usermessagebox.h"
#include "qtcore_helpers/displayformatting.hpp"
#include "shelloperations/cshelloperationrunner.h"
#include "shelloperations/shellfileoperations.h"
#include "theme/cthemecontroller.h"
#include "theme/ctintedsvgiconengine.h"
#include "timing/ctimeelapsed.h"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"
#include "widgets/ctabstrip.h"
#include "widgets/widgetutils.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cpanelwidget.h"

#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QDebug>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImage>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPolygon>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStyle>
#include <QToolTip>
#include <QUrl>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

// Set while the panel is the active one. themes/stylesheet.cpp has rules for it.
static constexpr char activePanelProperty[] = "activePanel";

// The inner line of a double frame is this far inside the outer one
static constexpr int doubleFrameLineOffset = 3;

// Both lines and the gap between them included
[[nodiscard]] static int flatFrameWidth(const Theme& theme)
{
	return theme.panelFrame == PanelFrame::Double ? doubleFrameLineOffset + 1 : 1;
}

// A frame's line, `inset` px inside its outer line `outer`. Where the current tab is open into the frame, the line runs up around it.
[[nodiscard]] static QPolygon frameLine(const QRect& outer, const std::optional<QRect>& openTab, const int inset)
{
	const QRect box = outer.adjusted(inset, inset, -inset, -inset);
	QPolygon line;
	line << box.bottomLeft() << box.topLeft();
	if (openTab)
	{
		const QRect tab = openTab->adjusted(inset, inset, -inset, 0);
		line << QPoint{ tab.left(), box.top() } << tab.topLeft() << tab.topRight() << QPoint{ tab.right(), box.top() };
	}
	line << box.topRight() << box.bottomRight();
	return line;
}

CPanelWidget::CPanelWidget(QWidget *parent) noexcept :
	QWidget(parent),
	ui(new Ui::CPanelWidget)
{
	ui->setupUi(this);
	setAttribute(Qt::WA_StyledBackground); // A QWidget subclass paints no stylesheet background or border without it

	ui->_infoLabel->clear();
	ui->_driveInfoLabel->clear();
	ui->_driveUsageBar->hide();
	assert_r(connect(ui->_driveButtonsWidget, &CDriveBar::volumeClicked, this, [this](const uint64_t volumeId) {
		if (const auto message = _controller->switchToVolume(_panelPosition, volumeId))
			showUserMessage(this, *message);

		ui->_list->setFocus();
	}));

	const auto dimTextColor = [] { return QApplication::palette().color(QPalette::PlaceholderText); };
	ui->_btnFavs->setIcon(tintedSvgIcon(QSL(":/theme/star.svg"), dimTextColor));
	ui->_btnToRoot->setIcon(tintedSvgIcon(QSL(":/theme/go_to_root.svg"), dimTextColor));

	assert_r(connect(ui->_breadcrumbBar, &CBreadcrumbBar::pathEntered, this, &CPanelWidget::goToEnteredPath));
	assert_r(connect(ui->_breadcrumbBar, &CBreadcrumbBar::editingCancelled, this, [this] { ui->_list->setFocus(); }));
	assert_r(connect(ui->_breadcrumbBar, &CBreadcrumbBar::goToAncestorRequested, this, [this](const QString& ancestorPath, const QString& childOnPath) {
		ui->_list->setFocus();
		// The cursor goes to the folder the path led through, where it still exists
		if (const CFileSystemObject child{ childOnPath }; child.exists())
			_controller->goToItem(_panelPosition, child);
		else
			_controller->setPath(_panelPosition, ancestorPath, refreshCauseOther);
	}));
	assert_r(connect(ui->_breadcrumbBar, &CBreadcrumbBar::folderMenuRequested, this, &CPanelWidget::fillFolderMenu));

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

	_frameGlow = std::make_unique<CGlowUnderlay>(*this, [this] { return frameGlow(); });
	// A TabsOnFrame frame runs around the current tab
	assert_r(connect(ui->_tabBar, &CTabStrip::currentChanged, this, &CPanelWidget::updateFrame));
	assert_r(connect(ui->_tabBar, &CTabStrip::currentTabGeometryChanged, this, &CPanelWidget::updateFrame));

	onSettingsChanged();

	applyTheme();
	assert_r(connect(&CThemeController::instance(), &CThemeController::themeChanged, this, &CPanelWidget::applyTheme));

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
	if (_iconProvider)
		_iconProvider->removeIconsChangedListener(this);

	delete ui;
}

void CPanelWidget::init(CController* controller, CShellOperationRunner& shellOperations, CIconProvider& iconProvider)
{
	assert_debug_only(controller);
	_controller = controller;
	_shellOperations = &shellOperations;
	_iconProvider = &iconProvider;

	_iconProvider->addIconsChangedListener(this);
}

void CPanelWidget::setFocusToFileList()
{
	ui->_list->setFocus();
}

void CPanelWidget::focusPathNavigatorWithPathSelected()
{
	ui->_breadcrumbBar->startEditing();
}

void CPanelWidget::setShownAsActivePanel(const bool active)
{
	ui->_list->setShownAsActivePanel(active);

	if (property(activePanelProperty).toBool() == active)
		return;

	setProperty(activePanelProperty, active);
	// A stylesheet matches property selectors only while polishing a widget; the tabs and the drive buttons have rules on this panel's property too
	std::vector<QWidget*> widgets{ this };
	for (const QWidget* buttonParent : std::array<const QWidget*, 2>{ ui->_tabBar, ui->_driveButtonsWidget })
	{
		for (QPushButton* button : buttonParent->findChildren<QPushButton*>(Qt::FindDirectChildrenOnly))
			widgets.push_back(button);
	}
	for (QWidget* widget : widgets)
	{
		style()->unpolish(widget);
		style()->polish(widget);
	}
	ui->_breadcrumbBar->setShownAsActive(active);
	ui->_driveUsageBar->setShownAsActive(active);
	updateFrame();
}

void CPanelWidget::paintEvent(QPaintEvent* /*event*/)
{
	const Theme& theme = appliedTheme();
	if (theme.structure != ThemeStructure::Flat)
		return; // the stylesheet draws the border of a rounded panel

	const FlatFrame frame = flatFrame();
	const bool active = property(activePanelProperty).toBool();
	const CBasePalette& palette = theme.palette;
	const bool tabsOnFrame = theme.panelLayout == PanelLayout::TabsOnFrame;
	QPainter painter{ this };
	// Covers the inside of the frame: the glow, which an ancestor paints, must show outside it only
	if (tabsOnFrame)
		painter.fillPath(outlineOf(frame), active && theme.fileList.activeBackground.isValid() ? theme.fileList.activeBackground : palette.surface);

	painter.setPen(active ? palette.accent : palette.border);
	const int lineCount = theme.panelFrame == PanelFrame::Double ? 2 : 1;
	for (int line = 0; line < lineCount; ++line)
	{
		const int inset = line * doubleFrameLineOffset;
		painter.drawPolygon(frameLine(frame.outerLine, frame.openTab, inset));
		if (frame.closedTab)
			painter.drawPolygon(frameLine(*frame.closedTab, std::nullopt, inset));
	}

	if (tabsOnFrame)
	{
		// Midway between the file list and the drives, from one side's innermost line to the other's
		const int y = (ui->_list->geometry().bottom() + ui->driveRowLayout->geometry().top()) / 2;
		const int inset = flatFrameWidth(theme);
		painter.setPen(palette.borderStrong);
		painter.drawLine(frame.outerLine.left() + inset, y, frame.outerLine.right() - inset, y);
	}
}

void CPanelWidget::saveTabViewStates() const
{
	TabColumnLayouts layouts;
	layouts.reserve(_tabs.size());
	for (int i = 0; i < (int)_tabs.size(); ++i)
		layouts.emplace_back(tabIdAt(i), columnLayoutOfTab(i));

	saveTabColumnLayouts(_panelPosition, layouts);
}

CFileListView::ColumnLayout CPanelWidget::columnLayoutOfTab(int index) const
{
	// A tab's stored layout is only refreshed when switching away from it, so for the active one the view's is newer.
	return index == _activeTab ? ui->_list->columnLayout() : _tabs[(size_t)index].columnLayout;
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

	ui->_tabBar->installEventFilter(this); // For the tabs' tooltips, and for the frame, which runs along the strip's bottom
	assert_r(connect(ui->_tabBar, &CTabStrip::currentChanged, this, &CPanelWidget::onTabBarCurrentChanged));
	assert_r(connect(ui->_tabBar, &CTabStrip::tabCloseRequested, this, &CPanelWidget::onTabBarCloseRequested));
	assert_r(connect(ui->_tabBar, &CTabStrip::tabMoved, this, &CPanelWidget::onTabBarTabMoved));
	assert_r(connect(ui->_tabBar, &CTabStrip::customContextMenuRequested, this, &CPanelWidget::showContextMenuForTab));

	// Mirror however many tabs CController restored for this side (usually one, but persisted multi-tab state may have more).
	const std::vector<qulonglong> ids = _controller->tabIds(p);
	const qulonglong activeId = _controller->activeTabId(p);

	const TabColumnLayouts storedLayouts = loadTabColumnLayouts(p);
	// With no layout read, the side's legacy header state applies to every tab, sorting included
	const LegacySideHeaderState legacyState = storedLayouts.empty() ? loadLegacySideHeaderState(p) : LegacySideHeaderState{};
	const auto restoredColumnLayout = [&](qulonglong tabId) {
		const auto stored = std::find_if(storedLayouts.cbegin(), storedLayouts.cend(), [tabId](const auto& layout) { return layout.first == tabId; });
		return stored != storedLayouts.cend() ? stored->second : legacyState.columnLayout;
	};

	int activeIndex = 0;
	for (const qulonglong id : ids)
	{
		if (legacyState.sorting)
			_controller->setTabSorting(p, id, legacyState.sorting->first, legacyState.sorting->second);

		if (const int index = appendTab(id, restoredColumnLayout(id)); id == activeId)
			activeIndex = index;
	}
	{
		const QSignalBlocker block(ui->_tabBar);
		ui->_tabBar->setCurrentIndex(activeIndex);
	}
	activateTab(activeIndex);
	updateTabBarVisibility();

	// Replaces the legacy state at once: a later start would otherwise migrate its sorting again, over newer sorting
	if (legacyState.sorting)
		saveTabViewStates();

	_controller->setPanelContentsChangedListener(p, this);
	_controller->setVolumesChangedListener(this);
	_controller->setCurrentItemChangedListener(p, this);
}

int CPanelWidget::appendTab(const qulonglong tabId, const CFileListView::ColumnLayout& columnLayout)
{
	PanelTab& tab = _tabs.emplace_back();
	tab.model = new CFileListModel(_iconProvider, FileTimeTextLength::Short, this);
	tab.model->setDropHandler([this](const QList<QUrl>& urls, Qt::DropAction action, const QString& destinationPath) {
		return dropUrls(urls, action, destinationPath);
	});
	assert_r(connect(tab.model, &CFileListModel::itemEdited, this, &CPanelWidget::renameItem));
	assert_r(connect(tab.model, &QAbstractItemModel::modelAboutToBeReset, ui->_list, &CFileListView::modelAboutToBeReset));
	assert_r(connect(tab.model, &CFileListModel::sorted, ui->_list, [this](){
		ui->_list->scrollTo(ui->_list->currentIndex());
	}));
	// Only the model on screen is sorted by the header
	assert_r(connect(tab.model, &CFileListModel::sorted, this, [this, model = tab.model, tabId]() {
		if (model == _model)
			_controller->setTabSorting(_panelPosition, tabId, sortFieldOf(model->sortColumn()), model->sortOrder());
	}));

	const CPanel& coreTab = _controller->tabById(_panelPosition, tabId);
	tab.model->sort(columnOf(coreTab.sortField()), coreTab.sortOrder());
	tab.columnLayout = columnLayout;

	// Each tab owns its selection model (parented to the widget, not the view) so it survives the view->setModel() swaps.
	tab.selectionModel = new QItemSelectionModel(tab.model, this);
	assert_r(connect(tab.selectionModel, &QItemSelectionModel::selectionChanged, this, &CPanelWidget::selectionChanged));
	assert_r(connect(tab.selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged));

	int index = 0;
	{
		const QSignalBlocker block(ui->_tabBar);
		index = ui->_tabBar->addTab(QString());
		ui->_tabBar->setTabData(index, tabId);
	}
	updateTabText(index);
	return index;
}

void CPanelWidget::activateTab(int index)
{
	assert_and_return_r(index >= 0 && index < (int)_tabs.size(), );
	_activeTab = index;
	PanelTab& tab = _tabs[(size_t)index];
	_model = tab.model;
	_selectionModel = tab.selectionModel;

	// setModel() sorts the new model by the header's sort indicator (a direct call, not a signal): the indicator must hold this tab's sort first.
	// Signals blocked: the indicator change would re-sort the model still attached.
	QHeaderView* header = ui->_list->header();
	{
		const QSignalBlocker blocker(header);
		header->setSortIndicator(_model->sortColumn(), _model->sortOrder());
	}

	ui->_list->setModel(_model);
	ui->_list->setSelectionModel(_selectionModel);
	ui->_list->applyColumnLayout(tab.columnLayout);

	// The filter box is shared by the tabs: it shows only while this tab has a filter.
	// Without one, the box keeps its last text for showFilterEditor() to offer again.
	const QString nameFilter = _controller->tabById(_panelPosition, tabIdAt(index)).nameFilter();
	if (!nameFilter.isEmpty())
		_filterDialog->setText(nameFilter);
	_filterDialog->setVisible(!nameFilter.isEmpty());

	// Show the now-active panel's contents immediately (the CPanel may also refresh asynchronously on activation).
	fillFromPanel(refreshCauseOther);
	// setModel() left another tab's scroll position, which a refresh keeps
	if (!ui->_list->restoreScrollPosition(std::exchange(tab.scrollPosition, {})))
		ui->_list->scrollTo(ui->_list->currentIndex());

	emit activeTabChanged();
}

void CPanelWidget::saveActiveTabViewState()
{
	if (_activeTab < 0 || _activeTab >= (int)_tabs.size())
		return;

	PanelTab& tab = activeTab();
	tab.columnLayout = ui->_list->columnLayout();
	tab.scrollPosition = ui->_list->scrollPosition();
}

void CPanelWidget::createNewTab()
{
	openPathInNewTab(_controller->panel(_panelPosition).currentDirPathPosix(), /*activate=*/true, _activeTab);
}

void CPanelWidget::openCurrentItemInNewTab()
{
	tryOpenItemInNewTab(_selectionModel->currentIndex(), /*activate=*/true);
}

void CPanelWidget::onItemMiddleClicked(const QModelIndex& index)
{
	// Middle-click opens a background tab (browser-style): don't switch the current panel away from what it's showing.
	tryOpenItemInNewTab(index, /*activate=*/false);
}

void CPanelWidget::tryOpenItemInNewTab(const QModelIndex& index, bool activate)
{
	if (!index.isValid())
		return;

	const CFileSystemObject& row = _model->rowAt(index);
	if (!row.isDir())
		return;

	// For [..] this opens the parent folder ('..' is cleaned out of the path by QFileInfo)
	openPathInNewTab(row.fullAbsolutePath(), activate, _activeTab);
}

void CPanelWidget::openPathInNewTab(const QString& path, bool activate, int sourceIndex)
{
	const CFileListView::ColumnLayout columnLayout = columnLayoutOfTab(sourceIndex);
	const qulonglong id = _controller->addTab(_panelPosition, path, activate, tabIdAt(sourceIndex));
	const int index = appendTab(id, columnLayout);
	updateTabBarVisibility();

	if (activate)
		ui->_tabBar->setCurrentIndex(index); // Activates through onTabBarCurrentChanged()
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
	const QString path = _controller->takeLastClosedTabPath(_panelPosition);
	if (path.isEmpty())
		return;

	// The closed tab's own appearance went with it, so the new tab looks like the one on screen.
	openPathInNewTab(path, /*activate=*/true, _activeTab);
}

void CPanelWidget::switchTabBy(const int step)
{
	const int count = (int)_tabs.size();
	if (count <= 1)
		return;
	ui->_tabBar->setCurrentIndex(((_activeTab + step) % count + count) % count);
}

void CPanelWidget::switchToTabByPosition(int position)
{
	if (position < 0 || position >= (int)_tabs.size())
		return;
	ui->_tabBar->setCurrentIndex(position);
}

void CPanelWidget::onTabBarCurrentChanged(int index)
{
	if (index < 0 || index >= (int)_tabs.size())
		return;

	saveActiveTabViewState();

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

	const int index = tabIndexById(id);
	if (index < 0)
		return;

	// activateTab() below restores the active tab's stored view state, so an active tab that stays open must store its live one first.
	// Before the erase: _activeTab still indexes that tab.
	if (index != _activeTab)
		saveActiveTabViewState();

	_controller->closeTab(_panelPosition, id);
	const qulonglong activeId = _controller->activeTabId(_panelPosition); // post-removal active tab

	// The closing tab's models are deleted only once activateTab() below has detached the view from them
	const PanelTab closing = _tabs[(size_t)index];
	_tabs.erase(_tabs.begin() + index);
	int controllerActiveIndex = 0;
	{
		const QSignalBlocker block(ui->_tabBar);
		ui->_tabBar->removeTab(index);
		// The removal shifted the positions after it, so the active tab is found by id
		controllerActiveIndex = tabIndexById(activeId);
		assert_r(controllerActiveIndex >= 0);
		ui->_tabBar->setCurrentIndex(controllerActiveIndex);
	}
	updateTabBarVisibility();
	activateTab(controllerActiveIndex);

	delete closing.selectionModel;
	delete closing.model;
}

void CPanelWidget::onTabBarTabMoved(int from, int to)
{
	if (from == to)
		return;

	// The tab bar has already moved the tab and its tab data: _tabs mirrors the move to stay position-aligned
	PanelTab movedTab = std::move(_tabs[(size_t)from]);
	_tabs.erase(_tabs.begin() + from);
	_tabs.insert(_tabs.begin() + to, std::move(movedTab));

	if (_activeTab == from)
		_activeTab = to;
	else if (from < _activeTab && to >= _activeTab)
		--_activeTab; // Shifted left into the gap
	else if (from > _activeTab && to <= _activeTab)
		++_activeTab; // Shifted right to make room

	_controller->moveTabPosition(_panelPosition, tabIdAt(to), (size_t)to);
}

void CPanelWidget::showContextMenuForTab(QPoint pos)
{
	const int index = ui->_tabBar->tabAt(pos);
	if (index < 0)
		return;

	QMenu menu;
	QAction* duplicateTabAction = menu.addAction(tr("Duplicate tab"));
	QAction* copyPathAction = menu.addAction(tr("Copy path to clipboard"));
	QAction* closeTabAction = menu.addAction(tr("Close tab") + QSL("\tMMB")); // The part after '\t' renders in the shortcut column (display only)
	closeTabAction->setEnabled(_tabs.size() > 1);
	QAction* closeOthersAction = menu.addAction(tr("Close all other tabs"));
	closeOthersAction->setEnabled(_tabs.size() > 1);

	const QAction* chosenAction = menu.exec(ui->_tabBar->mapToGlobal(pos));
	if (chosenAction == duplicateTabAction)
		duplicateTab(index);
	else if (chosenAction == copyPathAction)
		QApplication::clipboard()->setText(shellQuotedPath(_controller->tabById(_panelPosition, tabIdAt(index)).currentDirPathNative()));
	else if (chosenAction == closeTabAction)
		onTabBarCloseRequested(index);
	else if (chosenAction == closeOthersAction)
		closeAllOtherTabs(index);
}

void CPanelWidget::duplicateTab(int index)
{
	// Browser-style background tab, like middle-click: don't switch away from what's currently showing.
	openPathInNewTab(_controller->tabPath(_panelPosition, tabIdAt(index)), /*activate=*/false, index);
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
	ui->_tabBar->setVisible(_tabs.size() > 1 || appliedTheme().panelLayout == PanelLayout::TabsOnFrame);
	ui->_tabBar->setTabsClosable(_tabs.size() > 1);
}

void CPanelWidget::updateTabText(int index)
{
	if (index >= 0 && index < ui->_tabBar->count())
		ui->_tabBar->setTabText(index, _controller->tabName(_panelPosition, tabIdAt(index)));
}

QString CPanelWidget::tabToolTipText(int index) const
{
	const CPanel& tab = _controller->tabById(_panelPosition, tabIdAt(index));
	const QString path = tab.currentDirPathNative();

	// A tab that has never been activated has no listing, so there are no stats to show until it's been visited.
	const std::optional<FolderContentsSummary> contents = tab.contentsSummary();
	if (!contents)
		return path;

	return path % '\n' % tr("%1 folders, %2 files (%3)").arg(contents->numFolders).arg(contents->numFiles).arg(fileSizeToString(contents->size));
}

qulonglong CPanelWidget::tabIdAt(int index) const
{
	return ui->_tabBar->tabData(index);
}

int CPanelWidget::tabIndexById(const qulonglong id) const
{
	for (int i = 0; i < ui->_tabBar->count(); ++i)
	{
		if (tabIdAt(i) == id)
			return i;
	}

	return -1;
}

CPanelWidget::PanelTab& CPanelWidget::activeTab()
{
	assert_debug_only(_activeTab >= 0 && _activeTab < (int)_tabs.size());
	return _tabs[(size_t)_activeTab];
}

bool CPanelWidget::fillFromList(FileListRefreshCause operation)
{
	const QModelIndex previousCurrentIndex = _selectionModel->currentIndex();
	const int previousCurrentRow = previousCurrentIndex.row();
	const qulonglong previousCurrentHash = _model->itemHash(previousCurrentIndex);

	const CPanel& panel = _controller->panel(_panelPosition);
	std::vector<CFileSystemObject> rows;
	QString listedFolder;
	bool listed = false;
	panel.readCommittedContents([&rows, &listedFolder, &listed](const QString& folder, const FileListHashMap& items) {
		listed = true;
		listedFolder = folder;
		rows.reserve(items.size());
		for (const auto& item : items)
			rows.push_back(item.second);
	});

	PanelTab& tab = activeTab();
	const std::optional<uint64_t> navigationId = listed ? std::optional{ panel.navigationId() } : std::nullopt;
	// A refresh keeps the scroll position, the cursor, the selection and an open editor; a navigation starts over
	const bool refresh = navigationId && navigationId == tab.navigationId;
	tab.navigationId = navigationId;
	if (!refresh)
		tab.cursorTargetHash = 0;

	bool modelReset = true;
	{
		const QScopedValueRollback refilling{ _refillingList, true };
		if (refresh)
		{
			const CFileListView::ScrollPosition scrollPosition = ui->_list->scrollPosition();
			modelReset = !_model->updateRows(std::move(rows));
			ui->_list->restoreScrollPosition(scrollPosition);
		}
		else
			_model->setRows(std::move(rows), std::move(listedFolder));

		placeCursorAfterFill(operation, modelReset, previousCurrentRow, previousCurrentHash);
	}
	recordCurrentItem(_selectionModel->currentIndex());

	return modelReset;
}

void CPanelWidget::placeCursorAfterFill(const FileListRefreshCause operation, const bool modelReset, const int previousRow, const qulonglong previousHash)
{
	if (const QModelIndex cursorTarget = takeListedCursorTarget(); cursorTarget.isValid())
	{
		ui->_list->moveCursorToItem(cursorTarget);
		return;
	}

	if (!modelReset)
	{
		// The cursor follows its item; when the item is gone, it stays on the item's row
		const QModelIndex currentIndex = _selectionModel->currentIndex();
		if (!currentIndex.isValid() || _model->itemHash(currentIndex) != previousHash)
			ui->_list->moveCursorToItem(_model->index(std::min(std::max(previousRow, 0), _model->rowCount() - 1), 0));
		return;
	}

	auto indexUnderCursor = _model->index(0, 0);

	// Stepping up is not a special case here: setPath() has already recorded the folder we came from as the current item for the folder we arrived at.
	if (operation != refreshCauseForwardNavigation || QSettings().value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, INTERFACE_RESPECT_LAST_CURSOR_POS_DEFAULT).toBool())
	{
		const qulonglong itemHashToSetCursorTo = _controller->currentItemHashForFolder(_panelPosition, _controller->panel(_panelPosition).currentDirPathPosix());
		const QModelIndex itemIndexToSetCursorTo = _model->indexByHash(itemHashToSetCursorTo);
		if (itemIndexToSetCursorTo.isValid())
			indexUnderCursor = itemIndexToSetCursorTo;
		else
		{
			if (itemHashToSetCursorTo != 0)
				qInfo() << "Failed to find hash" << itemHashToSetCursorTo << "in" << currentDirPathNative();

			if (previousRow >= 0 && operation != refreshCauseCdUp && operation != refreshCauseForwardNavigation)
				indexUnderCursor = _model->index(std::min(previousRow, _model->rowCount() - 1), 0);
		}
	}

	ui->_list->moveCursorToItem(indexUnderCursor);
}

QModelIndex CPanelWidget::takeListedCursorTarget()
{
	qulonglong& targetHash = activeTab().cursorTargetHash;
	if (targetHash == 0)
		return {};

	const QModelIndex target = _model->indexByHash(targetHash);
	if (target.isValid())
		targetHash = 0;

	return target;
}

void CPanelWidget::fillFromPanel(FileListRefreshCause operation)
{
	const auto fillStart = std::chrono::steady_clock::now();

	const bool modelReset = fillFromList(operation);
	// After an update too: what the view's selection does through moves and removals is Qt's to decide
	applyCoreSelection();

	ui->_breadcrumbBar->setPath(_controller->panel(_panelPosition).currentDirPathPosix());
	fillHistory();
	updateCurrentVolumeButtonAndInfo();

	// Only a reset is sure to repaint the list
	if (modelReset)
		ui->_list->logTimeToNextPaint(fillStart);
}

void CPanelWidget::showContextMenuForItems()
{
	const CPanel& panel = _controller->panel(_panelPosition);
	std::vector<std::wstring> paths;
	for (const CFileSystemObject& item : panel.selectedItems())
		paths.push_back(item.fullAbsolutePath().toStdWString());

	if (paths.empty())
		paths.push_back(panel.currentDirPathNative().toStdWString());

	OsShell::openShellContextMenuForObjects(paths, WidgetUtils::nativeOwnerWinId(this));
}

void CPanelWidget::onSpacePressed()
{
	if (const QModelIndex itemIndex = _selectionModel->currentIndex(); itemIndex.isValid())
		_controller->displayDirSize(_panelPosition, _model->itemHash(itemIndex));
	invertCurrentItemSelection();
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

void CPanelWidget::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
	if (_applyingCoreSelection)
		return;

	// This doesn't let the user select the [..] item
	for (const QItemSelectionRange& range : selected)
	{
		for (int row = range.top(); row <= range.bottom(); ++row)
		{
			if (_model->rowAt(row).isCdUp())
			{
				_selectionModel->select(_model->index(row, 0), QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
				break;
			}
		}
	}

	const auto itemHashes = [this](const QItemSelection& selection) {
		std::vector<qulonglong> hashes;
		for (const QItemSelectionRange& range : selection)
		{
			for (int row = range.top(); row <= range.bottom(); ++row)
				hashes.push_back(_model->rowAt(row).hash());
		}
		return hashes;
	};

	CPanel& panel = _controller->panel(_panelPosition);
	panel.setItemsSelected(itemHashes(deselected), false);
	panel.setItemsSelected(itemHashes(selected), true);
	updateInfoLabel();
}

void CPanelWidget::applyCoreSelection()
{
	std::vector<int> rows;
	for (const qulonglong hash : _controller->panel(_panelPosition).selectedItemHashes())
	{
		if (const QModelIndex index = _model->indexByHash(hash); index.isValid())
			rows.push_back(index.row());
	}

	// Runs of adjacent rows as single ranges: a large selection is usually a few runs
	std::ranges::sort(rows);
	QItemSelection selection;
	for (size_t runBegin = 0, numRows = rows.size(); runBegin < numRows;)
	{
		size_t runEnd = runBegin + 1;
		while (runEnd < numRows && rows[runEnd] == rows[runEnd - 1] + 1)
			++runEnd;

		selection.select(_model->index(rows[runBegin], 0), _model->index(rows[runEnd - 1], 0));
		runBegin = runEnd;
	}

	{
		const QScopedValueRollback guard{ _applyingCoreSelection, true };
		CTimeElapsed timer{ true };
		_selectionModel->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		if (const auto elapsedMs = timer.elapsed(); elapsedMs >= 100)
			qInfo() << "_selectionModel->select took" << elapsedMs << "ms for" << rows.size() << "items";
	}

	updateInfoLabel();
}

void CPanelWidget::currentItemChanged(const QModelIndex& current, const QModelIndex& /*previous*/)
{
	if (_refillingList)
		return;

	// Any cursor move other than a refill's overrides the target
	activeTab().cursorTargetHash = 0;
	recordCurrentItem(current);
}

void CPanelWidget::recordCurrentItem(const QModelIndex& current)
{
	// An invalid index means the view has no contents to put the cursor on, not that the user moved it off every item.
	// Recording it would erase the folder's remembered current item, including the one a pending navigation just set.
	if (!current.isValid())
		return;

	const qulonglong hash = _model->itemHash(current);
	_controller->setCurrentItemHashForCurrentFolder(_panelPosition, hash, false);

	// Every refill re-places the cursor, usually on the same item
	if (hash == _lastSignalledCurrentItemHash)
		return;

	_lastSignalledCurrentItemHash = hash;
	emit currentItemChangedSignal(_panelPosition, hash);
}

void CPanelWidget::onCurrentItemChanged(Panel p, qulonglong tabId, const QString& folder, qulonglong currentItemHash)
{
	if (!displaysTab(p, tabId))
		return;

	if (ui->_list->editingInProgress())
		return; // Can't move cursor while editing is in progress, it crashes inside Qt

	// Queued, so the tab may have navigated away from the folder this item belongs to
	if (_controller->panel(_panelPosition).currentDirObject().fullAbsolutePath() != folder)
		return;

	// The item is often not listed yet: fillFromList() then places the cursor once it is
	activeTab().cursorTargetHash = currentItemHash;
	if (const QModelIndex cursorTarget = takeListedCursorTarget(); cursorTarget.isValid())
		ui->_list->moveCursorToItem(cursorTarget);
}

void CPanelWidget::renameItem(const qulonglong hash, const QString& newName)
{
	const CFileSystemObject item = _controller->itemByHash(_panelPosition, hash);
	if (item.isCdUp())
		return;

	auto* mainWindow = CMainWindow::get();
	assert_and_return_r(mainWindow, );
	mainWindow->closeViewedFilesWithin({ item.fullAbsolutePath() });

	auto result = _controller->renameItem(_panelPosition, hash, newName, InlineRenameReplacement::Ask);
	if (result.status == InlineRenameStatus::ReplacementRequired)
	{
		// Fix for https://github.com/VioletGiraffe/file-commander/issues/123
		const UserMessage question = inlineRenameReplacementQuestion(result, newName);
		if (QMessageBox::question(this, question.title, question.text, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
			return;

		result = _controller->renameItem(_panelPosition, hash, newName, InlineRenameReplacement::Replace);
	}

	if (const auto message = inlineRenameMessage(result, item.fullName().toString(), newName))
		showUserMessage(this, *message);
}

void CPanelWidget::toRoot()
{
	if (const auto currentVolume = _controller->currentVolumeInfo(_panelPosition))
		_controller->setPath(_panelPosition, currentVolume->rootObjectInfo.fullAbsolutePath(), refreshCauseOther);
}

void CPanelWidget::showFavoriteLocationsMenu(QPoint pos)
{
	QMenu menu;
	fillFavoriteLocationsMenu(menu, _controller->favoriteLocations(), _controller->panel(_panelPosition).currentDirPathPosix(), *this, [this](const QString& path) {
		_controller->setPath(_panelPosition, path, refreshCauseOther);
	});

	if (menu.exec(pos)) // Something was selected
		setFocusToFileList(); // #84
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
	_filterDialog->showForEditing();
	filterTextEdited(_filterDialog->text());
}

void CPanelWidget::filterTextEdited(const QString& filterText)
{
	// The core deselects what the filter hides; the view follows when the selection is copied from the core
	CPanel& panel = _controller->panel(_panelPosition);
	panel.setNameFilter(filterText);
	_model->setNameFilter(filterText);
	applyCoreSelection();

	// Hiding the cursor's row drops the view's cursor, but not the core's current item: the cursor returns once the item shows again
	if (!_selectionModel->currentIndex().isValid())
	{
		if (const QModelIndex current = _model->indexByHash(panel.currentItem().hash()); current.isValid())
			ui->_list->moveCursorToItem(current);
	}
}

void CPanelWidget::filterTextConfirmed(const QString& filterText)
{
	filterTextEdited(filterText);
	ui->_list->setFocus();
}

void CPanelWidget::copySelectionToClipboard() const
{
	putSelectionOnClipboard(false);
}

void CPanelWidget::cutSelectionToClipboard() const
{
	putSelectionOnClipboard(true);
}

void CPanelWidget::putSelectionOnClipboard(bool cut) const
{
	const std::vector<CFileSystemObject> items = _controller->panel(_panelPosition).selectedItemsOrCurrentItem();
	if (items.empty())
		return; // Leaves the clipboard as it was

	putFilesOnClipboard(items, cut, *this);
}

void CPanelWidget::pasteSelectionFromClipboard(bool specialPaste)
{
	const QMimeData* clipboardData = QApplication::clipboard()->mimeData();
	if (!clipboardData)
		return;

	// If the clipboard contains an image (not a file), paste it into a file
	if (clipboardData->hasImage())
	{
		if (const auto message = saveImageAsNewFile(qvariant_cast<QImage>(clipboardData->imageData()), currentDirPathNative(), specialPaste))
			showUserMessage(this, *message);
		return;
	}

#ifndef _WIN32
	dropUrls(clipboardData->urls(), clipboardFilesDropAction(*clipboardData), {});
#else
	auto* hwnd = WidgetUtils::nativeOwnerWinId(this);
	_shellOperations->run([hwnd, folder = currentDirPathNative()] {
		if (!pasteFromClipboardWithShell(folder, hwnd))
			log_unconditional_r("The shell's Paste failed in " + folder.toStdString());
	});
#endif
}

void CPanelWidget::goToEnteredPath(const QString& path)
{
	ui->_list->setFocus();
	if (const auto message = _controller->goToTypedPath(_panelPosition, path))
		showUserMessage(this, *message);
}

static QString withTrailingSlash(const QString& path)
{
	return path.endsWith('/') ? path : path + '/';
}

void CPanelWidget::fillHistory()
{
	const std::vector<QString> folders = _controller->recentFolders(_panelPosition);
	if (folders.empty())
		return;

	QStringList items;
	items.reserve((qsizetype)folders.size());
	for (const QString& folder : folders)
		items.push_back(toNativeSeparators(withTrailingSlash(folder)));

	// The active tab's current directory might not be in the list yet (e.g. a deferred save hasn't
	// caught up) -- prepend it so it's still shown/highlighted correctly instead of defaulting to some
	// unrelated entry.
	const QString currentDir = _controller->panel(_panelPosition).currentDirPathPosix();
	const QString currentDirDisplay = toNativeSeparators(withTrailingSlash(currentDir));
	qsizetype currentDirRow = items.indexOf(currentDirDisplay);
	if (currentDirRow < 0)
	{
		items.push_front(currentDirDisplay);
		currentDirRow = 0;
	}

	ui->_breadcrumbBar->setHistory(items, (int)currentDirRow);
}

void CPanelWidget::fillFolderMenu(QMenu* menu, const QString& folderPath)
{
	static constexpr size_t maxVisitedItems = 25, maxSubfolderItems = 20;

	const auto addFolderItem = [this, menu](const QString& text, const QString& path) {
		QAction* const action = menu->addAction(text);
		assert_r(connect(action, &QAction::triggered, this, [this, path] {
			ui->_list->setFocus();
			_controller->setPath(_panelPosition, path, refreshCauseOther);
		}));
	};

	// The subfolders on the way to visited folders, in the order of the path editor's history
	std::unordered_set<QString> seen;
	for (const QString& visited : _controller->recentFolders(_panelPosition))
	{
		if (seen.size() >= maxVisitedItems)
			break;

		const QString path = withTrailingSlash(visited);
		if (path.size() <= folderPath.size() || !path.startsWith(folderPath))
			continue;

		const qsizetype nameEnd = path.indexOf('/', folderPath.size()); // found: the path ends with '/'
		if (QString subfolderPath = path.left(nameEnd + 1); seen.insert(subfolderPath).second)
			addFolderItem(path.sliced(folderPath.size(), nameEnd - folderPath.size()), subfolderPath);
	}

	// Listed on the UI thread: an ancestor of a folder just listed is unlikely to stall
	const auto entries = listDirectoryWithDetails(folderPath);
	if (!entries)
		return;

	// A subfolder the visited section already has is left out
	const bool showHiddenFiles = _controller->showHiddenFiles();
	std::vector<QString> subfolderNames;
	for (const auto& entry : *entries)
	{
		const CFileSystemObject object{ folderPath, entry };
		if (object.isDir() && (showHiddenFiles || !object.isHidden()) && !seen.contains(object.fullAbsolutePath()))
			subfolderNames.push_back(object.fullName().toString());
	}

	if (subfolderNames.empty())
		return;

	const size_t shownCount = std::min(subfolderNames.size(), maxSubfolderItems);
	std::ranges::partial_sort(subfolderNames, subfolderNames.begin() + (ptrdiff_t)shownCount, NaturalSort::lessCaseInsensitive);

	if (!menu->isEmpty())
		menu->addSeparator();
	for (size_t i = 0; i < shownCount; ++i)
		addFolderItem(subfolderNames[i], folderPath + subfolderNames[i]);

	if (subfolderNames.size() > shownCount)
		menu->addAction(tr("... %1 more").arg(subfolderNames.size() - shownCount))->setEnabled(false);
}

void CPanelWidget::updateInfoLabel()
{
	const CPanel& panel = _controller->panel(_panelPosition);
	const FolderContentsSummary& total = _model->contentsSummary();
	const FolderContentsSummary selected = panel.selectionSummary();

	// Coloured values make the summary rich text, with the translated words escaped
	const QColor valueColor = appliedTheme().summaryValueColor;
	const bool richText = valueColor.isValid();
	const auto words = [richText](const QString& s) { return richText ? s.toHtmlEscaped() : s; };
	const auto value = [&](const QString& s) { return richText ? QStringLiteral("<span style=\"color: %1\">%2</span>").arg(valueColor.name(), s) : s; };

	QString text = words(tr("%1/%2 files, %3/%4 folders selected (%5 / %6)")).arg(
		value(QString::number(selected.numFiles)), value(QString::number(total.numFiles)),
		value(QString::number(selected.numFolders)), value(QString::number(total.numFolders)),
		value(fileSizeToString(selected.size)), value(fileSizeToString(total.size)));
	if (panel.displayMode() == CPanel::SearchResultsMode)
		text.prepend(words(tr("Search results: ")));
	ui->_infoLabel->setTextFormat(richText ? Qt::RichText : Qt::PlainText);
	ui->_infoLabel->setText(text);
}

bool CPanelWidget::fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item)
{
	assert_and_return_r(item.isValid(), false);
	emit itemActivated(_model->itemHash(item), this);
	return true; // Consuming the event
}

void CPanelWidget::volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool significantChange) noexcept
{
	if (p != _panelPosition)
		return;

	if (significantChange)
		ui->_driveButtonsWidget->setVolumes(drives);

	updateCurrentVolumeButtonAndInfo();
}

void CPanelWidget::currentVolumeChanged(Panel p) noexcept
{
	if (p != _panelPosition)
		return;

	updateCurrentVolumeButtonAndInfo();
}

bool CPanelWidget::dropUrls(const QList<QUrl>& urls, Qt::DropAction action, const QString& destinationPath)
{
	if (action != Qt::CopyAction && action != Qt::MoveAction)
		return false;

	std::vector<CFileSystemObject> objects;
	for (const QUrl& url : urls)
	{
		if (url.isLocalFile())
			objects.emplace_back(url.toLocalFile());
	}

	if (objects.empty())
		return false;

	CFileSystemObject destination{ destinationPath.isEmpty() ? currentDirPathNative() : destinationPath };
	if (destination.isFile())
		destination = CFileSystemObject(destination.parentDirPath());
	assert_and_return_r(destination.exists() && destination.isDir(), false);

	auto* mainWindow = CMainWindow::get();
	assert_and_return_r(mainWindow, false);

	return mainWindow->launchFileTransfer(action == Qt::MoveAction ? TransferKind::Move : TransferKind::Copy, std::move(objects), destination.fullAbsolutePath());
}

bool CPanelWidget::eventFilter(QObject * object, QEvent * e)
{
	if (object == ui->_list && e->type() == QEvent::ContextMenu)
	{
		showContextMenuForItems();
		return true;
	}
	else if(object == ui->_list->viewport() && e->type() == QEvent::Wheel)
	{
		const auto* wEvent = static_cast<QWheelEvent*>(e);
		if (wEvent->modifiers() == Qt::ShiftModifier)
		{
			if (wEvent->angleDelta().y() > 0)
				_controller->navigateBack(_panelPosition);
			else
				_controller->navigateForward(_panelPosition);
			return true;
		}
	}
	else if (object == ui->_list->viewport() && e->type() == QEvent::ToolTip)
	{
		// Not the model's ToolTipRole: the model stays independent of the OS shell
		const auto* helpEvent = static_cast<QHelpEvent*>(e);
		const QModelIndex index = ui->_list->indexAt(helpEvent->pos());
		if (!index.isValid())
		{
			QToolTip::hideText();
			return true;
		}

		const CFileSystemObject& row = _model->rowAt(index);
		const QString toolTip = row.fullName() % "\n\n" % QString::fromStdWString(OsShell::toolTip(row.fullAbsolutePath().toStdWString()));
		QToolTip::showText(helpEvent->globalPos(), toolTip, ui->_list->viewport(), ui->_list->visualRect(index));
		return true;
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
	else if (object == ui->_tabBar && e->type() == QEvent::ToolTip)
	{
		// Composed on demand: the active tab's folder changes under it
		const auto* helpEvent = static_cast<QHelpEvent*>(e);
		const int index = ui->_tabBar->tabAt(helpEvent->pos());
		if (index >= 0)
			QToolTip::showText(helpEvent->globalPos(), tabToolTipText(index), ui->_tabBar);
		else
			QToolTip::hideText();
		return true;
	}
	else if (object == ui->_tabBar && (e->type() == QEvent::Resize || e->type() == QEvent::Move))
		updateFrame();

	return QWidget::eventFilter(object, e);
}

void CPanelWidget::onPanelContentsChanged(Panel p, qulonglong tabId, FileListRefreshCause operation)
{
	if (!displaysTab(p, tabId))
		return;

	fillFromPanel(operation);
	updateTabText(_activeTab);
}

void CPanelWidget::onPanelContentsInvalidated(Panel p, qulonglong tabId)
{
	if (!displaysTab(p, tabId))
		return;

	// Display only - see PanelContentsChangedListener. The tab is already at the new folder, so its name is shown
	// right away instead of lagging behind for however long the listing takes.
	_model->setRows({}, {});
	PanelTab& tab = activeTab();
	tab.navigationId.reset();
	tab.cursorTargetHash = 0;
	updateInfoLabel();
	updateTabText(_activeTab);
}

// Only the tab on screen can repaint; switching to another re-attaches the shared view, which refetches every row.
void CPanelWidget::onIconsChanged(const std::vector<qulonglong>& objectHashes)
{
	if (_model)
		_model->onIconsChanged(objectHashes);
}

void CPanelWidget::onAllIconsChanged()
{
	if (_model)
		_model->onAllIconsChanged();
}

// Every tab of this side notifies us, but only one of them is on screen.
bool CPanelWidget::displaysTab(Panel p, qulonglong tabId) const
{
	return p == _panelPosition && tabId == tabIdAt(_activeTab);
}

CFileListView *CPanelWidget::fileListView() const
{
	return ui->_list;
}

QAbstractItemModel * CPanelWidget::model() const
{
	return _model;
}

qulonglong CPanelWidget::currentItemHash() const
{
	return _model->itemHash(_selectionModel->currentIndex());
}

void CPanelWidget::invertSelection()
{
	_controller->panel(_panelPosition).invertSelection();
	applyCoreSelection();
}

void CPanelWidget::setItemsSelectedByPattern(const QString& patternText, const bool selected, const bool filesOnly)
{
	_controller->panel(_panelPosition).setItemsSelectedByPattern(patternText, selected, filesOnly);
	applyCoreSelection();
}

void CPanelWidget::selectFilesWithExtensionOfCurrentItem()
{
	_controller->panel(_panelPosition).selectFilesWithExtensionOf(currentItemHash());
	applyCoreSelection();
}

void CPanelWidget::moveCursorToFirstFile()
{
	const int row = _model->firstFileRow();
	if (row >= 0)
		ui->_list->moveCursorToItem(_model->index(row, 0));
}

void CPanelWidget::copySelectedItemsPathsToClipboard() const
{
	if (const QString pathLines = _controller->panel(_panelPosition).selectedOrCurrentItemPathLines(); !pathLines.isEmpty())
		QApplication::clipboard()->setText(pathLines);
}

void CPanelWidget::onSettingsChanged()
{
	setFileListFontFromSettings();
	ui->_list->setColumnSeparatorsShown(QSettings{}.value(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, INTERFACE_SHOW_COLUMN_SEPARATORS_DEFAULT).toBool());
}

void CPanelWidget::setFileListFontFromSettings()
{
	QFont font = fileListFont();
	if (const std::optional<int> size = tabletModeFontPointSize())
		font.setPointSize(*size);
	ui->_list->setFont(font);
}

void CPanelWidget::applyTheme()
{
	const Theme& theme = appliedTheme();
	const bool flat = theme.structure == ThemeStructure::Flat;
	assert_r(flat == (theme.panelLayout != PanelLayout::Stacked));

	arrangeRows(theme.panelLayout);
	// PathInFrame: the path field centred and as wide as its text
	for (QSpacerItem* spacer : { ui->pathRowLeftSpacer, ui->pathRowRightSpacer })
		spacer->changeSize(0, 0, theme.panelLayout == PanelLayout::PathInFrame ? QSizePolicy::Expanding : QSizePolicy::Fixed, QSizePolicy::Minimum);
	ui->pathRowLayout->invalidate();
	ui->_breadcrumbBar->applyTheme();
	updateTabBarVisibility();

	// The file list has no padding: its rows reach the panel's border. The stylesheet pads the info label.
	// The margins are the border's width. A flat panel's top edge is behind its first row.
	const int padding = theme.metrics.panelPadding;
	const int border = flat ? flatFrameWidth(theme) : theme.metrics.panelBorderWidth;
	ui->verticalLayout->setContentsMargins(border, flat ? 0 : border, border, border);
	ui->verticalLayout->setSpacing(theme.metrics.panelRowSpacing);
	ui->driveRowLayout->setContentsMargins(padding, flat ? 0 : padding, padding, 0);
	ui->_driveButtonsWidget->setButtonSpacing(theme.metrics.driveButtonSpacing);
	ui->_tabBar->setSpacing(theme.metrics.tabSpacing);
	ui->pathRowLayout->setContentsMargins(padding, 0, padding, 0);
	ui->tabRowLayout->setContentsMargins(padding, 0, padding, 0);

	FileListLook look = theme.fileList;
	look.cursorGlowRadius = theme.metrics.glowRadius;
	ui->_list->setLook(look);
	// A stylesheet change restores the font each widget had when first polished
	setFileListFontFromSettings();
	updateFrame();
	// The summary's values take the theme's colour. _model is null until initPanel() shows a tab.
	if (_model)
		updateInfoLabel();
}

void CPanelWidget::updateFrame()
{
	update();
	_frameGlow->refresh();
}

void CPanelWidget::arrangeRows(const PanelLayout layout)
{
	QBoxLayout& rows = *ui->verticalLayout;
	QLayoutItem* const drives = ui->driveRowLayout, * const path = ui->pathRowLayout, * const tabs = ui->tabRowLayout;
	QLayoutItem* const list = rows.itemAt(rows.indexOf(ui->_list)), * const info = rows.itemAt(rows.indexOf(ui->_infoLabel));
	assert_and_return_r(list && info, );

	using Order = std::array<QLayoutItem*, 5>;
	const Order order = layout == PanelLayout::Stacked ? Order{ drives, path, tabs, list, info }
		: layout == PanelLayout::PathInFrame ? Order{ path, drives, tabs, list, info }
		: Order{ tabs, path, list, drives, info };

	for (int i = 0; i < (int)order.size(); ++i)
	{
		QLayoutItem* const item = order[(size_t)i];
		if (rows.itemAt(i) == item)
			continue;

		rows.removeItem(item);
		// A child layout must go back through insertLayout(): removing it took it from this layout's children
		if (QLayout* const childLayout = item->layout())
			rows.insertLayout(i, childLayout);
		else
			rows.insertItem(i, item);
	}
}

CPanelWidget::FlatFrame CPanelWidget::flatFrame() const
{
	const Theme& theme = appliedTheme();
	FlatFrame frame;

	int top = 0;
	if (theme.panelLayout == PanelLayout::PathInFrame)
		top = ui->_breadcrumbBar->geometry().center().y(); // the path row's widgets cover the part of the edge behind them
	else if (const int current = ui->_tabBar->currentIndex(); ui->_tabBar->isVisibleTo(this) && current >= 0)
	{
		const QRect strip = ui->_tabBar->geometry();
		top = strip.bottom();
		const QRect tab = ui->_tabBar->tabRect(current).translated(strip.topLeft());
		(tab.bottom() == strip.bottom() ? frame.openTab : frame.closedTab) = tab;
	}

	frame.outerLine = QRect{ QPoint{ 0, top }, QPoint{ width() - 1, height() - 1 } };
	return frame;
}

QPainterPath CPanelWidget::outlineOf(const FlatFrame& frame)
{
	QPainterPath outline;
	outline.addPolygon(frameLine(frame.outerLine, frame.openTab, 0));
	outline.closeSubpath();
	if (frame.closedTab)
		outline.addRect(QRectF{ *frame.closedTab });
	return outline;
}

std::optional<CGlowUnderlay::Glow> CPanelWidget::frameGlow() const
{
	const Theme& theme = appliedTheme();
	if (theme.structure != ThemeStructure::Flat || theme.metrics.glowRadius <= 0 || !property(activePanelProperty).toBool())
		return {};

	return CGlowUnderlay::Glow{ .shape = outlineOf(flatFrame()), .color = theme.palette.accent, .radius = theme.metrics.glowRadius };
}

void CPanelWidget::updateCurrentVolumeButtonAndInfo()
{
	const auto currentVolumeInfo = _controller->currentVolumeInfo(_panelPosition);
	ui->_driveButtonsWidget->setCurrentVolume(currentVolumeInfo ? std::optional{ currentVolumeInfo->id() } : std::nullopt);
	if (currentVolumeInfo)
	{
		const auto sizeText = [](const uint64_t size) { return fileSizeToString(size, { .significantPlaces = 3, .decimalPlaces = 2 }); }; // such as 212.43 GiB
		const uint64_t volumeSize = currentVolumeInfo->volumeSize, freeSize = currentVolumeInfo->freeSize;
		ui->_driveInfoLabel->setText(tr("%1 (%2 free)").arg(sizeText(volumeSize), sizeText(freeSize)));
		const QString& fileSystemName = currentVolumeInfo->fileSystemName;
		ui->_driveInfoLabel->setToolTip(fileSystemName.isEmpty() ? currentVolumeInfo->volumeLabel : currentVolumeInfo->volumeLabel + " (" + fileSystemName + ')');

		const uint64_t usedSize = volumeSize - std::min(freeSize, volumeSize);
		const double usedFraction = volumeSize > 0 ? (double)usedSize / (double)volumeSize : 0.0;
		ui->_driveUsageBar->setSpace(volumeSize, freeSize);
		ui->_driveUsageBar->setToolTip(tr("%1% used (%2)").arg(std::lround(usedFraction * 100.0)).arg(sizeText(usedSize)));
		ui->_driveUsageBar->setVisible(volumeSize > 0);
	}
	else
	{
		ui->_driveInfoLabel->clear();
		ui->_driveInfoLabel->setToolTip({});
		ui->_driveUsageBar->hide();
	}
}

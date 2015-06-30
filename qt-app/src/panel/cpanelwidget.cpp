#include "cpanelwidget.h"
#include "filelistwidget/cfilelistview.h"
#include "filelistwidget/model/cfilelistmodel.h"
#include "ui_cpanelwidget.h"
#include "qflowlayout.h"
#include "shell/cshell.h"
#include "columns.h"
#include "filelistwidget/model/cfilelistsortfilterproxymodel.h"
#include "pluginengine/cpluginengine.h"
#include "../favoritelocationseditor/cfavoritelocationseditor.h"
#include "widgets/clineedit.h"
#include "filesystemhelperfunctions.h"
#include "progressdialogs/ccopymovedialog.h"
#include "../cmainwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QClipboard>
#include <QDateTime>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <time.h>
#include <set>
#include <tuple>

CPanelWidget::CPanelWidget(QWidget *parent /* = 0 */) :
	QWidget(parent),
	_filterDialog(this),
	ui(new Ui::CPanelWidget),
	_controller (CController::get()),
	_selectionModel(0),
	_model(0),
	_sortModel(0),
	_panelPosition(UnknownPanel),
	_calcDirSizeShortcut(QKeySequence(Qt::Key_Space), this, SLOT(calcDirectorySize()), 0, Qt::WidgetWithChildrenShortcut),
	_selectCurrentItemShortcut(QKeySequence(Qt::Key_Insert), this, SLOT(invertCurrentItemSelection()), 0, Qt::WidgetWithChildrenShortcut),
	_showFilterEditorShortcut(QKeySequence("Ctrl+F"), this, SLOT(showFilterEditor()), 0, Qt::WidgetWithChildrenShortcut),
	_copyShortcut(QKeySequence("Ctrl+C"), this, SLOT(copySelectionToClipboard()), 0, Qt::WidgetWithChildrenShortcut),
	_cutShortcut(QKeySequence("Ctrl+X"), this, SLOT(cutSelectionToClipboard()), 0, Qt::WidgetWithChildrenShortcut),
	_pasteShortcut(QKeySequence("Ctrl+V"), this, SLOT(pasteSelectionFromClipboard()), 0, Qt::WidgetWithChildrenShortcut)
{
	ui->setupUi(this);

	ui->_infoLabel->clear();
	ui->_driveInfoLabel->clear();

	ui->_pathNavigator->setLineEdit(new CLineEdit);
	ui->_pathNavigator->setHistoryMode(true);
	connect(ui->_pathNavigator, SIGNAL(activated(QString)), SLOT(pathFromHistoryActivated(QString)));
	connect(ui->_pathNavigator, SIGNAL(itemActivated(QString)), SLOT(pathFromHistoryActivated(QString)));

	connect(ui->_list, SIGNAL(contextMenuRequested(QPoint)), SLOT(showContextMenuForItems(QPoint)));
	connect(ui->_list, SIGNAL(keyPressed(QString,int,Qt::KeyboardModifiers)), SLOT(fileListViewKeyPressed(QString,int,Qt::KeyboardModifiers)));

	connect(ui->_driveInfoLabel, SIGNAL(doubleClicked(QPoint)), SLOT(showFavoriteLocationsMenu(QPoint)));
	connect(ui->_btnFavs, &QPushButton::clicked, [&]{showFavoriteLocationsMenu(mapToGlobal(ui->_btnFavs->geometry().bottomLeft()));});
	connect(ui->_btnToRoot, SIGNAL(clicked()), SLOT(toRoot()));

	connect(&_filterDialog, SIGNAL(filterTextChanged(QString)), SLOT(filterTextChanged(QString)));

	ui->_list->addEventObserver(this);
}

CPanelWidget::~CPanelWidget()
{
	delete ui;
}

void CPanelWidget::setFocusToFileList()
{
	ui->_list->setFocus();
}

QByteArray CPanelWidget::savePanelState() const
{
	return ui->_list->header()->saveState();
}

bool CPanelWidget::restorePanelState(QByteArray state)
{
	if (!state.isEmpty())
	{
		ui->_list->setHeaderAdjustmentRequired(false);
		return ui->_list->header()->restoreState(state);
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

bool CPanelWidget::restorePanelGeometry(QByteArray state)
{
	return ui->_list->header()->restoreGeometry(state);
}

QString CPanelWidget::currentDir() const
{
	return _controller.panel(_panelPosition).currentDirPathNative();
}

Panel CPanelWidget::panelPosition() const
{
	return _panelPosition;
}

void CPanelWidget::setPanelPosition(Panel p)
{
	assert(_panelPosition == UnknownPanel);
	_panelPosition = p;

	ui->_list->installEventFilter(this);
	ui->_list->viewport()->installEventFilter(this);
	ui->_list->setPanelPosition(p);

	_model = new(std::nothrow) CFileListModel(ui->_list, this);
	_model->setPanelPosition(p);
	connect(_model, SIGNAL(itemEdited(qulonglong,QString)), SLOT(itemNameEdited(qulonglong,QString)));

	_sortModel = new(std::nothrow) CFileListSortFilterProxyModel(this);
	_sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	_sortModel->setFilterRole(FullNameRole);
	_sortModel->setPanelPosition(p);
	_sortModel->setSourceModel(_model);

	ui->_list->setModel(_sortModel);
	connect(_sortModel, SIGNAL(modelAboutToBeReset()), ui->_list, SLOT(modelAboutToBeReset()));
	connect(_sortModel, &CFileListSortFilterProxyModel::sorted, ui->_list, [=](){
		ui->_list->scrollTo(ui->_list->currentIndex());
	});

	_selectionModel = ui->_list->selectionModel(); // can only be called after setModel
	assert(_selectionModel);
	connect(_selectionModel, SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(selectionChanged(QItemSelection,QItemSelection)));
	connect(_selectionModel, SIGNAL(currentChanged(QModelIndex,QModelIndex)), SLOT(currentItemChanged(QModelIndex,QModelIndex)));

	_controller.setPanelContentsChangedListener(p, this);

	fillHistory();

	_controller.setDisksChangedListener(this);
}

// Returns the list of items added to the view
void CPanelWidget::fillFromList(const std::map<qulonglong, CFileSystemObject>& items, bool sameDirAsPrevious, FileListRefreshCause operation)
{
	time_t start = clock();
	const auto globalStart = start;

	// Remembering currently highlighted item, as well as current folder, to restore cursor afterwards
	const qulonglong currentHash = currentItemHash();
	const QModelIndex currentIndex = _selectionModel->currentIndex();
	const QString previousFolder = _directoryCurrentlyBeingDisplayed;

	//ui->_list->setUpdatesEnabled(false);
	ui->_list->saveHeaderState();
	_sortModel->setSourceModel(nullptr);
	_model->clear();

	_model->setColumnCount(NumberOfColumns);
	_model->setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Ext") << tr("Size") << tr("Date"));

	int itemRow = 0;
	std::vector<std::tuple<int, FileListViewColumns, QStandardItem*>> qTreeViewItems;
	qTreeViewItems.reserve(items.size() * NumberOfColumns);

	for (const auto& item: items)
	{
		const CFileSystemObject& object = item.second;
		auto props = object.properties();

		QStandardItem * fileNameItem = new QStandardItem();
		fileNameItem->setEditable(false);
		if (props.type == Directory)
			fileNameItem->setData(QString("[" % (object.isCdUp() ? QString("..") : props.fullName) % "]"), Qt::DisplayRole);
		else if (props.completeBaseName.isEmpty() && props.type == File) // File without a name, displaying extension in the name field and adding point to extension
			fileNameItem->setData(QString('.') + props.extension, Qt::DisplayRole);
		else
			fileNameItem->setData(props.completeBaseName, Qt::DisplayRole);
		fileNameItem->setIcon(object.icon());
		fileNameItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, NameColumn, fileNameItem);

		QStandardItem * fileExtItem = new QStandardItem();
		fileExtItem->setEditable(false);
		if (!props.completeBaseName.isEmpty() && !props.extension.isEmpty())
			fileExtItem->setData(props.extension, Qt::DisplayRole);
		fileExtItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, ExtColumn, fileExtItem);

		QStandardItem * sizeItem = new QStandardItem();
		sizeItem->setEditable(false);
		if (props.type != Directory || props.size > 0)
			sizeItem->setData(fileSizeToString(props.size), Qt::DisplayRole);
		sizeItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, SizeColumn, sizeItem);

		QStandardItem * dateItem = new QStandardItem();
		dateItem->setEditable(false);
		QDateTime modificationDate;
		modificationDate.setTime_t((uint)props.modificationDate);
		modificationDate = modificationDate.toLocalTime();
		dateItem->setData(modificationDate.toString("dd.MM.yyyy hh:mm"), Qt::DisplayRole);
		dateItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, DateColumn, dateItem);

		++itemRow;
	}

	qDebug () << __FUNCTION__ << "Creating" << items.size() << "items took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	start = clock();
	for (const auto& qTreeViewItem: qTreeViewItems)
		_model->setItem(std::get<0>(qTreeViewItem), std::get<1>(qTreeViewItem), std::get<2>(qTreeViewItem));
	qDebug () << __FUNCTION__ << "Setting" << items.size() << "items to the model took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	start = clock();
	_sortModel->setSourceModel(_model);
	qDebug () << __FUNCTION__ << "Setting the source model to sort model took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	ui->_list->restoreHeaderState();

	ui->_list->moveCursorToItem(_sortModel->index(0, 0));

	// Setting the cursor position as appropriate
	if (sameDirAsPrevious && operation != refreshCauseNewItemCreated)
	{
		QModelIndex indexToMoveCursorTo;
		if (currentHash != 0)
			indexToMoveCursorTo = indexByHash(currentHash);
		if (!indexToMoveCursorTo.isValid())
			indexToMoveCursorTo = currentIndex;

		if (indexToMoveCursorTo.isValid() && currentIndex.row() < ui->_list->model()->rowCount())
		{
			ui->_list->moveCursorToItem(indexToMoveCursorTo);
		}
	}
	else if (operation == refreshCauseCdUp)
	{
		// Setting the folder we've just stepped out of as current
		qulonglong targetFolderHash = 0;
		for (auto& item: items)
		{
			if (item.second.fullAbsolutePath() == previousFolder)
			{
				targetFolderHash = item.first;
				break;
			}
		}

		if (targetFolderHash != 0)
			ui->_list->moveCursorToItem(indexByHash(targetFolderHash));
	}
	else if (operation != refreshCauseForwardNavigation) // refreshCauseNewItemCreated must fall into this branch, among other things
	{
		const qulonglong lastVisitedItemInDirectory = _controller.currentItemInFolder(_panelPosition, _controller.panel(_panelPosition).currentDirPathNative());
		if (lastVisitedItemInDirectory != 0)
		{
			const QModelIndex lastVisitedIndex = indexByHash(lastVisitedItemInDirectory);
			if (lastVisitedIndex.isValid())
				ui->_list->moveCursorToItem(lastVisitedIndex);
		}
	}

	selectionChanged(QItemSelection(), QItemSelection());

	qDebug () << __FUNCTION__ << items.size() << "items," << (clock() - globalStart) * 1000 / CLOCKS_PER_SEC << "ms";
}

void CPanelWidget::fillFromPanel(const CPanel &panel, FileListRefreshCause operation)
{
	const auto itemList = panel.list();
	const auto previousSelection = selectedItemsHashes(true);
	std::set<qulonglong> selectedItemsHashes; // For fast search
	for (auto hash = previousSelection.begin(); hash != previousSelection.end(); ++hash)
		selectedItemsHashes.insert(*hash);

	fillFromList(itemList, toPosixSeparators(panel.currentDirPathNative()) == _directoryCurrentlyBeingDisplayed, operation);
	_directoryCurrentlyBeingDisplayed = toPosixSeparators(panel.currentDirPathNative());

	// Restoring previous selection
	if (!selectedItemsHashes.empty())
		for (int row = 0; row < _sortModel->rowCount(); ++row)
		{
			const qulonglong hash = hashByItemRow(row);
			if (selectedItemsHashes.count(hash) != 0)
				_selectionModel->select(_sortModel->index(row, 0), QItemSelectionModel::Rows | QItemSelectionModel::Select);
		}

	fillHistory();
	updateCurrentDiskButton();
}

void CPanelWidget::showContextMenuForItems(QPoint pos)
{
	const auto selection = selectedItemsHashes(true);
	std::vector<std::wstring> paths;
	if (selection.empty())
		paths.push_back(_controller.panel(_panelPosition).currentDirPathNative().toStdWString());
	else
	{
		for (size_t i = 0; i < selection.size(); ++i)
		{
			if (!_controller.itemByHash(_panelPosition, selection[i]).isCdUp() || selection.size() == 1)
			{
				QString selectedItemPath = _controller.itemPath(_panelPosition, selection[i]);
				paths.push_back(selectedItemPath.toStdWString());
			}
			else if (!selection.empty())
			{
				// This is a cdup element ([..]), and we should remove selection from it
				_selectionModel->select(indexByHash(selection[i]), QItemSelectionModel::Clear | QItemSelectionModel::Rows);
			}
		}
	}

	CShell::openShellContextMenuForObjects(paths, pos.x(), pos.y(), (void*)winId());
}

void CPanelWidget::showContextMenuForDisk(QPoint pos)
{
#ifdef _WIN32
	const QPushButton * button = dynamic_cast<const QPushButton*>(sender());
	if (!button)
		return;

	pos = button->mapToGlobal(pos);
	const int diskId = button->property("id").toInt();
	std::vector<std::wstring> diskPath(1, _controller.diskPath(diskId).toStdWString());
	CShell::openShellContextMenuForObjects(diskPath, pos.x(), pos.y(), (HWND)winId());
#else
	Q_UNUSED(pos);
#endif
}

void CPanelWidget::calcDirectorySize()
{
	QModelIndex itemIndex = _selectionModel->currentIndex();
	if (itemIndex.isValid())
	{
		_selectionModel->select(itemIndex, QItemSelectionModel::Toggle | QItemSelectionModel::Rows);
		_controller.displayDirSize(_panelPosition, hashByItemIndex(itemIndex));
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

	const int id = sender()->property("id").toInt();
	if (!_controller.switchToDisk(_panelPosition, id))
		QMessageBox::information(this, tr("Failed to switch disk"), tr("The disk %1 is inaccessible (locked or doesn't exist).").arg(_controller.diskPath(id)));

	ui->_list->setFocus();
}

void CPanelWidget::selectionChanged(QItemSelection selected, QItemSelection /*deselected*/)
{
	// This doesn't let the user select the [..] item

	const QString cdUpPath = CFileSystemObject(QFileInfo(currentDir())).parentDirPath();
	for (auto indexRange: selected)
	{
		auto indexList = indexRange.indexes();
		for (const auto& index: indexList)
		{
			const auto hash = hashByItemIndex(index);
			if (_controller.itemByHash(_panelPosition, hash).fullAbsolutePath() == cdUpPath)
			{
				auto cdUpIndex = indexByHash(hash);
				assert(cdUpIndex.isValid());
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

void CPanelWidget::currentItemChanged(QModelIndex current, QModelIndex /*previous*/)
{
	const qulonglong hash = current.isValid() ? hashByItemIndex(current) : 0;
	CPluginEngine::get().currentItemChanged(_panelPosition, hash);

	emit currentItemChanged(_panelPosition, hash);
}

void CPanelWidget::itemNameEdited(qulonglong hash, QString newName)
{
	CFileSystemObject item = _controller.itemByHash(_panelPosition, hash);

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationMove, std::vector<CFileSystemObject>(1, item), item.parentDirPath() + "/" + newName, CMainWindow::get());
	connect(CMainWindow::get(), SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->connect(dialog, SIGNAL(closed()), SLOT(deleteLater()));
	dialog->show();
}

void CPanelWidget::toRoot()
{
	if (!_currentDisk.isEmpty())
		_controller.setPath(_panelPosition, _currentDisk, refreshCauseOther);
}

void CPanelWidget::showFavoriteLocationsMenu(QPoint pos)
{
	QMenu menu;
	std::function<void(QMenu *, std::list<CLocationsCollection>&)> createMenus = [this, &createMenus](QMenu * parentMenu, std::list<CLocationsCollection>& locations)
	{
		for (auto& item: locations)
		{
			if (item.subLocations.empty() && !item.absolutePath.isEmpty())
			{
				QAction * action = parentMenu->addAction(item.displayName);
				const QString& path = item.absolutePath;
				if (CFileSystemObject(path) == CFileSystemObject(currentDir()))
				{
					action->setCheckable(true);
					action->setChecked(true);
				}

				QObject::connect(action, &QAction::triggered, [this, path](){
					_controller.setPath(_panelPosition, path, refreshCauseOther);
				});
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
		QObject::connect(addFolderAction, &QAction::triggered, [this, &locations](){
			const QString path = currentDir();
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

				_controller.favoriteLocations().addItem(locations, name, currentDir());
			}
		});

		QAction * addCategoryAction = parentMenu->addAction(tr("Add a new subcategory..."));
		QObject::connect(addCategoryAction, &QAction::triggered, [this, &locations, parentMenu](){
			const QString name = QInputDialog::getText(this, tr("Enter the name"), tr("Enter the name for the new subcategory"));
			if (!name.isEmpty())
			{
				if (std::find_if(locations.cbegin(), locations.cend(), [&name](const CLocationsCollection& entry){return entry.displayName == name;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), tr("Similar item already exists"), tr("An item with the same name already exists here (possibly pointing to a different location)."), QMessageBox::Cancel);
					return;
				}

				parentMenu->addMenu(name);
				_controller.favoriteLocations().addItem(locations, name);
			}
		});
	};

	createMenus(&menu, _controller.favoriteLocations().locations());
	menu.addSeparator();
	QAction * edit = menu.addAction(tr("Edit..."));
	connect(edit, SIGNAL(triggered()), SLOT(showFavoriteLocationsEditor()));
	menu.exec(pos);
}

void CPanelWidget::showFavoriteLocationsEditor()
{
	CFavoriteLocationsEditor(this).exec();
}

void CPanelWidget::fileListViewKeyPressed(QString keyText, int key, Qt::KeyboardModifiers modifiers)
{
	if (key == Qt::Key_Backspace)
	{
		// Navigating back
		_controller.navigateUp(_panelPosition);
	}
	else
	{
		emit fileListViewKeyPressedSignal(this, keyText, key, modifiers);
	}
}

void CPanelWidget::showFilterEditor()
{
	_filterDialog.showAt(ui->_list->geometry().bottomLeft());
}

void CPanelWidget::filterTextChanged(QString filterText)
{
	_sortModel->setFilterWildcard(filterText);
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
	std::vector<std::wstring> paths;
	auto hashes = selectedItemsHashes();
	for (auto hash: hashes)
		paths.emplace_back(_controller.itemByHash(_panelPosition, hash).fullAbsolutePath().toStdWString());

	CShell::copyObjectsToClipboard(paths, (void*)winId());
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
	for (auto hash: hashes)
		paths.emplace_back(_controller.itemByHash(_panelPosition, hash).fullAbsolutePath().toStdWString());

	CShell::cutObjectsToClipboard(paths, (void*)winId());
#endif
}

void CPanelWidget::pasteSelectionFromClipboard()
{
#ifndef _WIN32
	QClipboard * clipBoard = QApplication::clipboard();
	if (clipBoard)
	{
		const QMimeData * data = clipBoard->mimeData();
		_model->dropMimeData(clipBoard->mimeData(), (data && data->property("cut").toBool()) ? Qt::MoveAction : Qt::CopyAction, 0, 0, QModelIndex());
	}
#else
	_controller.execOnWorkerThread([this]() {
		CShell::pasteFromClipboard(currentDir().toStdWString(), (void*)winId());
	});
#endif
}

void CPanelWidget::pathFromHistoryActivated(QString path)
{
	if (_controller.setPath(_panelPosition, path, refreshCauseOther) == rcDirNotAccessible)
		QMessageBox::information(this, tr("Failed to set the path"), tr("The path %1 is inaccessible (locked or doesn't exist). Setting the closest accessible path instead.").arg(path));
}

void CPanelWidget::fillHistory()
{
	const auto& history = _controller.panel(_panelPosition).history();
	if (history.empty())
		return;

	ui->_pathNavigator->clear();
	for(auto it = history.rbegin(); it != history.rend(); ++it)
		ui->_pathNavigator->addItem(toNativeSeparators(it->endsWith("/") ? *it : (*it) + "/"));

	ui->_pathNavigator->setCurrentIndex(static_cast<int>(history.size() - 1 - history.currentIndex()));
}

void CPanelWidget::updateInfoLabel(const std::vector<qulonglong>& selection)
{
	ui->_infoLabel->clear();

	uint64_t numFilesSelected = 0, numFoldersSelected = 0, totalSize = 0, sizeSelected = 0, totalNumFolders = 0, totalNumFiles = 0;
	const auto currentTotalList = _controller.panel(_panelPosition).list();
	for (auto it = currentTotalList.begin(); it != currentTotalList.end(); ++it)
	{
		const CFileSystemObject& object = it->second;
		if (object.isFile())
			++totalNumFiles;
		else if (object.isDir())
			++totalNumFolders;
		totalSize += object.size();
	}

	for (auto it = selection.begin(); it != selection.end(); ++it)
	{
		const CFileSystemObject object = _controller.itemByHash(_panelPosition, *it);
		if (object.isFile())
			++numFilesSelected;
		else if (object.isDir())
			++numFoldersSelected;

		sizeSelected += object.size();
	}

	ui->_infoLabel->setText(tr("%1/%2 files, %3/%4 folders selected (%5 / %6)").arg(numFilesSelected).arg(totalNumFiles).
		arg(numFoldersSelected).arg(totalNumFolders).
		arg(fileSizeToString(sizeSelected)).arg(fileSizeToString(totalSize)));
}

bool CPanelWidget::fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item)
{
	assert(item.isValid());
	QModelIndex source = _sortModel->mapToSource(item);
	const qulonglong hash = _model->item(source.row(), source.column())->data(Qt::UserRole).toULongLong();
	emit itemActivated(hash, this);
	return true; // Consuming the event
}

void CPanelWidget::disksChanged(const std::vector<CDiskEnumerator::DiskInfo>& drives, Panel p)
{
	if (p != _panelPosition)
		return;

	_currentDisk.clear();

	if (!ui->_driveButtonsWidget->layout())
	{
		QFlowLayout * flowLayout = new QFlowLayout(ui->_driveButtonsWidget, 0, 0, 0);
		flowLayout->setSpacing(1);
		ui->_driveButtonsWidget->setLayout(flowLayout);
	}

	// Clearing and deleting the previous buttons
	QLayout * layout = ui->_driveButtonsWidget->layout();
	assert(layout);
	while (layout->count() > 0)
	{
		QWidget * w = layout->itemAt(0)->widget();
		layout->removeWidget(w);
		w->deleteLater();
	}

	// Creating and adding new buttons
	for (size_t i = 0; i < drives.size(); ++i)
	{
		const auto& driveInfo = drives[i].storageInfo;
		if (!driveInfo.isValid())
			continue;

#ifdef _WIN32
		const QString name = driveInfo.rootPath().remove(":/");
#else
		QString name = driveInfo.displayName();
		if (name.startsWith("/") && name.indexOf('/', 1) != -1)
		{
			const int lastPathPart = name.lastIndexOf('/');
			name = name.mid(lastPathPart + 1);
		}
#endif

		assert(layout);
		QPushButton * diskButton = new QPushButton;
		diskButton->setCheckable(true);
		diskButton->setIcon(drives[i].fileSystemObject.icon());
		diskButton->setText(name);
		diskButton->setFixedWidth(QFontMetrics(diskButton->font()).width(diskButton->text()) + 5 + diskButton->iconSize().width() + 20);
		diskButton->setProperty("id", (qulonglong)i);
		diskButton->setContextMenuPolicy(Qt::CustomContextMenu);
		diskButton->setToolTip(driveInfo.displayName());
		connect(diskButton, SIGNAL(clicked()), SLOT(driveButtonClicked()));
		connect(diskButton, SIGNAL(customContextMenuRequested(QPoint)), SLOT(showContextMenuForDisk(QPoint)));
		layout->addWidget(diskButton);
	}

	updateCurrentDiskButton();
}

qulonglong CPanelWidget::hashByItemIndex(const QModelIndex &index) const
{
	if (!index.isValid())
		return 0;
	QStandardItem * item = _model->item(_sortModel->mapToSource(index).row(), 0);
	assert(item);
	bool ok = false;
	const qulonglong hash = item->data(Qt::UserRole).toULongLong(&ok);
	assert(ok);
	return hash;
}

qulonglong CPanelWidget::hashByItemRow(const int row) const
{
	return hashByItemIndex(_sortModel->index(row, 0));
}

QModelIndex CPanelWidget::indexByHash(const qulonglong hash) const
{
	for(int row = 0; row < _sortModel->rowCount(); ++row)
		if (hashByItemRow(row) == hash)
			return _sortModel->index(row, 0);
	return QModelIndex();
}

bool CPanelWidget::eventFilter(QObject * object, QEvent * e)
{
	if (object == ui->_list)
	{
		switch (e->type())
		{
		case QEvent::ContextMenu:
			showContextMenuForItems(QCursor::pos()); // QCursor::pos() returns global pos
			return true;
		default:
			break;
		}
	}
	else if(e->type() == QEvent::Wheel && object == ui->_list->viewport())
	{
		QWheelEvent * wEvent = dynamic_cast<QWheelEvent*>(e);
		if (wEvent && wEvent->modifiers() == Qt::ShiftModifier)
		{
			if (wEvent->delta() > 0)
				_controller.navigateBack(_panelPosition);
			else
				_controller.navigateForward(_panelPosition);
			return true;
		}
	}
	return QWidget::eventFilter(object, e);
}

void CPanelWidget::panelContentsChanged(Panel p , FileListRefreshCause operation)
{
	if (p == _panelPosition)
		fillFromPanel(_controller.panel(_panelPosition), operation);
}

void CPanelWidget::itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress)
{
	if (p != _panelPosition)
		return;
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
		for (auto it = selection.begin(); it != selection.end(); ++it)
		{
			const qulonglong hash = hashByItemIndex(*it);
			if (!_controller.itemByHash(_panelPosition, hash).isCdUp())
				result.push_back(hash);
		}
	}
	else if (!onlyHighlightedItems)
	{
		auto currentIndex = _selectionModel->currentIndex();
		if (currentIndex.isValid())
		{
			const auto hash = hashByItemIndex(currentIndex);
			if (!_controller.itemByHash(_panelPosition, hash).isCdUp())
				result.push_back(hash);
		}
	}

	return result;
}

qulonglong CPanelWidget::currentItemHash() const
{
	QModelIndex currentIndex = _selectionModel->currentIndex();
	return hashByItemIndex(currentIndex);
}

void CPanelWidget::invertSelection()
{
	ui->_list->invertSelection();
}

void CPanelWidget::updateCurrentDiskButton()
{
	QLayout * layout = ui->_driveButtonsWidget->layout();
	if (!layout)
		return;

	for (int i = 0; i < layout->count(); ++i)
	{
		QLayoutItem* item = layout->itemAt(i);
		QPushButton* button = dynamic_cast<QPushButton*>(item ? item->widget() : nullptr);
		if (!button)
			continue;

		const size_t id = button->property("id").toULongLong();
		const size_t currentDriveId = _controller.currentDiskIndex(_panelPosition);
		if (id == currentDriveId)
		{
			button->setChecked(true);
			const auto& diskInfo = _controller.diskEnumerator().drives()[id];
			_currentDisk = diskInfo.fileSystemObject.fullAbsolutePath();
			ui->_driveInfoLabel->setText(tr("%1 (%2): %3 available, <b>%4 free</b> of %5 total").arg(diskInfo.storageInfo.displayName()).
				arg(QString::fromUtf8(diskInfo.storageInfo.fileSystemType())).
				arg(fileSizeToString(diskInfo.storageInfo.bytesAvailable(), 'M', " ")).
				arg(fileSizeToString(diskInfo.storageInfo.bytesFree(), 'M', " ")).
				arg(fileSizeToString(diskInfo.storageInfo.bytesTotal(), 'M', " ")));

			return;
		}
	}
}

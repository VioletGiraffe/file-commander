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

#include <assert.h>
#include <time.h>
#include <set>

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

	connect(ui->_btnFavs, SIGNAL(clicked()), SLOT(showFavoriteLocationsMenu()));
	connect(ui->_btnToRoot, SIGNAL(clicked()), SLOT(toRoot()));

	connect(&_filterDialog, SIGNAL(filterTextChanged(QString)), SLOT(filterTextChanged(QString)));

	ui->_list->addEventObserver(this);

	_controller.setDisksChangedListener(this);
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
	return _controller.panel(_panelPosition).currentDirPath();
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
}

// Returns the list of items added to the view
void CPanelWidget::fillFromList(const std::vector<CFileSystemObject> &items, bool sameDirAsPrevious, NavigationOperation operation)
{
	const time_t start = clock();

	// Remembering currently highlighted item, as well as current folder, to restore cursor afterwards
	const qulonglong currentHash = currentItemHash();
	const QModelIndex currentIndex = _selectionModel->currentIndex();
	const QString previousFolder = _directoryCurrentlyBeingDisplayed;

	ui->_list->saveHeaderState();
	_model->clear();
	_sortModel->setSourceModel(0);

	_model->setColumnCount(NumberOfColumns);
	_model->setHorizontalHeaderLabels(QStringList() << "Name" << "Ext" << "Size" << "Date");

	for (int i = 0; i < (int)items.size(); ++i)
	{
		auto props = items[i].properties();


		QStandardItem * fileNameItem = new QStandardItem();
		fileNameItem->setEditable(false);
		if (props.type == Directory)
			fileNameItem->setData(QString("[%1]").arg(items[i].isCdUp() ? QString("..") : props.fullName), Qt::DisplayRole);
		else if (props.completeBaseName.isEmpty() && props.type == File) // File without a name, displaying extension in the name field and adding point to extension
			fileNameItem->setData(QString('.') + props.extension, Qt::DisplayRole);
		else
			fileNameItem->setData(props.completeBaseName, Qt::DisplayRole);
		fileNameItem->setIcon(items[i].icon());
		fileNameItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		_model->setItem(i, NameColumn, fileNameItem);

		QStandardItem * fileExtItem = new QStandardItem();
		fileExtItem->setEditable(false);
		if (!props.completeBaseName.isEmpty() && !props.extension.isEmpty())
			fileExtItem->setData(props.extension, Qt::DisplayRole);
		fileExtItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		_model->setItem(i, ExtColumn, fileExtItem);

		QStandardItem * sizeItem = new QStandardItem();
		sizeItem->setEditable(false);
		if (props.type != Directory || props.size > 0)
			sizeItem->setData(fileSizeToString(props.size), Qt::DisplayRole);
		sizeItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		_model->setItem(i, SizeColumn, sizeItem);

		QStandardItem * dateItem = new QStandardItem();
		dateItem->setEditable(false);
		QDateTime modificationDate;
		modificationDate.setTime_t((uint)props.modificationDate);
		modificationDate = modificationDate.toLocalTime();
		dateItem->setData(modificationDate.toString("dd.MM.yyyy hh:mm"), Qt::DisplayRole);
		dateItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		_model->setItem(i, DateColumn, dateItem);
	}

	_sortModel->setSourceModel(_model);
	ui->_list->restoreHeaderState();

	ui->_list->moveCursorToItem(_sortModel->index(0, 0));

	if (sameDirAsPrevious)
	{
		QModelIndex indexToMoveCursorTo;
		if (currentHash != 0)
			indexToMoveCursorTo = indexByHash(currentHash);
		if (!indexToMoveCursorTo.isValid())
			indexToMoveCursorTo = currentIndex;

		if (indexToMoveCursorTo.isValid())
		{
			ui->_list->moveCursorToItem(indexToMoveCursorTo);
		}
	}
	else if (operation == nopCdUp)
	{
		// Setting the folder we've just stepped out of as current
		qulonglong targetFolderHash = 0;
		for (auto& item: items)
		{
			if (item.absoluteFilePath() == previousFolder)
			{
				targetFolderHash = item.hash();
				break;
			}
		}

		if (targetFolderHash != 0)
			ui->_list->moveCursorToItem(indexByHash(targetFolderHash));
	}
	else if (operation != nopForward)
	{
		const qulonglong lastVisitedItemInDirectory = _controller.currentItemInFolder(_panelPosition, _controller.panel(_panelPosition).currentDirPath());
		if (lastVisitedItemInDirectory != 0)
		{
			const QModelIndex lastVisitedIndex = indexByHash(lastVisitedItemInDirectory);
			if (lastVisitedIndex.isValid())
				ui->_list->moveCursorToItem(lastVisitedIndex);
		}
	}

	selectionChanged(QItemSelection(), QItemSelection());

	qDebug () << __FUNCTION__ << items.size() << "items," << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";
}

void CPanelWidget::fillFromPanel(const CPanel &panel, NavigationOperation operation)
{
	auto& itemList = panel.list();
	const auto previousSelection = selectedItemsHashes(true);
	std::set<qulonglong> selectedItemsHashes; // For fast search
	for (auto hash = previousSelection.begin(); hash != previousSelection.end(); ++hash)
		selectedItemsHashes.insert(*hash);

	fillFromList(itemList, toPosixSeparators(panel.currentDirPath()) == _directoryCurrentlyBeingDisplayed, operation);
	_directoryCurrentlyBeingDisplayed = toPosixSeparators(panel.currentDirPath());

	// Restoring previous selection
	if (!selectedItemsHashes.empty())
		for (int row = 0; row < _sortModel->rowCount(); ++row)
		{
			const qulonglong hash = hashByItemRow(row);
			if (selectedItemsHashes.count(hash) != 0)
				_selectionModel->select(_sortModel->index(row, 0), QItemSelectionModel::Rows | QItemSelectionModel::Select);
		}

	fillHistory();
}

void CPanelWidget::showContextMenuForItems(QPoint pos)
{
	const auto selection = selectedItemsHashes(true);
	std::vector<std::wstring> paths;
	if (selection.empty())
		paths.push_back(_controller.panel(_panelPosition).currentDirPath().toStdWString());
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
	const size_t diskId = size_t(button->property("id").toUInt());
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

	const size_t id = size_t(sender()->property("id").toUInt());
	_controller.diskSelected(_panelPosition, id);
}

void CPanelWidget::selectionChanged(QItemSelection /*selected*/, QItemSelection /*deselected*/)
{
	// This doesn't let the user select the [..] item
	auto selection = selectedItemsHashes();
	const QString cdUpPath = CFileSystemObject(QFileInfo(currentDir())).parentDirPath();
	for(auto it = selection.begin(); it != selection.end(); ++it)
	{
		if (_controller.itemByHash(_panelPosition, *it).absoluteFilePath() == cdUpPath)
		{
			auto cdUpIndex = indexByHash(*it);
			assert(cdUpIndex.isValid());
			_selectionModel->select(cdUpIndex, QItemSelectionModel::Deselect | QItemSelectionModel::Rows);
			selection.erase(it);
			break;
		}
	}

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
	CFileSystemObject& item = _controller.itemByHash(_panelPosition, hash);
	const auto result = item.rename(newName, true);
	if (result == rcTargetAlreadyExists)
		QMessageBox::warning(this, "Failed to rename a file", QString("Failed to rename \"") + item.fullName() + "\" to \"" + newName + "\" , target already exists");
	else if (result != rcOk)
		QMessageBox::warning(this, "Failed to rename a file", QString("Failed to rename \"") + item.fullName() + "\" to \"" + newName + "\"");
}

void CPanelWidget::toRoot()
{
	if (!_currentDisk.isEmpty())
		_controller.setPath(_panelPosition, _currentDisk, nopOther);
}

void CPanelWidget::showFavoriteLocationsMenu()
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
				QObject::connect(action, &QAction::triggered, [this, path](){
					_controller.setPath(_panelPosition, path, nopOther);
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

		QAction * addFolderAction = parentMenu->addAction("Add current folder here...");
		QObject::connect(addFolderAction, &QAction::triggered, [this, &locations](){
			const QString path = currentDir();
			const QString displayName = CFileSystemObject(path).name();
			const QString name = QInputDialog::getText(this, "Enter the name", "Enter the name to store the current location under", QLineEdit::Normal, displayName.isEmpty() ? path : displayName);
			if (!name.isEmpty() && !path.isEmpty())
			{
				if (std::find_if(locations.cbegin(), locations.cend(), [&path](const CLocationsCollection& entry){return entry.absolutePath == path;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), "Similar item already exists", "This item already exists here (possibly under a different name).", QMessageBox::Cancel);
					return;
				}
				else if (std::find_if(locations.cbegin(), locations.cend(), [&name](const CLocationsCollection& entry){return entry.displayName == name;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), "Similar item already exists", "And item with the same name already exists here (possibly pointing to a different location).", QMessageBox::Cancel);
					return;
				}

				_controller.favoriteLocations().addItem(locations, name, currentDir());
			}
		});

		QAction * addCategoryAction = parentMenu->addAction("Add a new subcategory...");
		QObject::connect(addCategoryAction, &QAction::triggered, [this, &locations, parentMenu](){
			const QString name = QInputDialog::getText(this, "Enter the name", "Enter the name for the new subcategory");
			if (!name.isEmpty())
			{
				if (std::find_if(locations.cbegin(), locations.cend(), [&name](const CLocationsCollection& entry){return entry.displayName == name;}) != locations.cend())
				{
					QMessageBox::information(dynamic_cast<QWidget*>(parent()), "Similar item already exists", "An item with the same name already exists here (possibly pointing to a different location).", QMessageBox::Cancel);
					return;
				}

				parentMenu->addMenu(name);
				_controller.favoriteLocations().addItem(locations, name);
			}
		});
	};

	createMenus(&menu, _controller.favoriteLocations().locations());
	menu.addSeparator();
	QAction * edit = menu.addAction("Edit...");
	connect(edit, SIGNAL(triggered()), SLOT(showFavoriteLocationsEditor()));
	menu.exec(mapToGlobal(ui->_btnFavs->geometry().bottomLeft()));
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
		paths.emplace_back(_controller.itemByHash(_panelPosition, hash).absoluteFilePath().toStdWString());
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
		paths.emplace_back(_controller.itemByHash(_panelPosition, hash).absoluteFilePath().toStdWString());
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
	CShell::pasteFromClipboard(currentDir().toStdWString(), (void*)winId());
#endif
}

void CPanelWidget::pathFromHistoryActivated(QString path)
{
	_controller.setPath(_panelPosition, path, nopOther);
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
	const auto& currentTotalList = _controller.panel(_panelPosition).list();
	for (auto it = currentTotalList.begin(); it != currentTotalList.end(); ++it)
	{
		if (it->isFile())
			++totalNumFiles;
		else if (it->isDir())
			++totalNumFolders;
		totalSize += it->size();
	}

	for (auto it = selection.begin(); it != selection.end(); ++it)
	{
		const CFileSystemObject& object = _controller.itemByHash(_panelPosition, *it);
		if (object.isFile())
			++numFilesSelected;
		else if (object.isDir())
			++numFoldersSelected;

		sizeSelected += object.size();
	}

	ui->_infoLabel->setText(QString("%1/%2 files, %3/%4 folders selected (%5 / %6)").arg(numFilesSelected).arg(totalNumFiles).
		arg(numFoldersSelected).arg(totalNumFolders).
		arg(fileSizeToString(sizeSelected)).arg(fileSizeToString(totalSize)));
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
			result.push_back(hash);
		}
	}
	else if (!onlyHighlightedItems)
	{
		auto currentIndex = _selectionModel->currentIndex();
		if (currentIndex.isValid())
			result.push_back(hashByItemIndex(currentIndex));
	}

	return result;
}

bool CPanelWidget::fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item)
{
	assert(item.isValid());
	QModelIndex source = _sortModel->mapToSource(item);
	const qulonglong hash = _model->item(source.row(), source.column())->data(Qt::UserRole).toULongLong();
	emit itemActivated(hash, this);
	return true; // Consuming the event
}

void CPanelWidget::disksChanged(QList<QStorageInfo> drives, Panel p, int currentDriveIndex)
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
	for (int i = 0; i < drives.size(); ++i)
	{
		QString name = drives[i].rootPath();
		if (name.endsWith(":/"))
			name.remove(":/");

		const CFileSystemObject fileSystemObject(drives[i].rootPath());

		assert(layout);
		QPushButton * diskButton = new QPushButton;
		diskButton->setCheckable(true);
		diskButton->setIcon(fileSystemObject.icon());
		diskButton->setText(name);
		diskButton->setFixedWidth(QFontMetrics(diskButton->font()).width(diskButton->text()) + 5 + diskButton->iconSize().width() + 20);
		diskButton->setProperty("id", quint64(i));
		diskButton->setContextMenuPolicy(Qt::CustomContextMenu);
		diskButton->setToolTip(drives[i].displayName());
		connect(diskButton, SIGNAL(clicked()), SLOT(driveButtonClicked()));
		connect(diskButton, SIGNAL(customContextMenuRequested(QPoint)), SLOT(showContextMenuForDisk(QPoint)));
		if (i == currentDriveIndex)
		{
			diskButton->setChecked(true);
			_currentDisk = fileSystemObject.absoluteFilePath();
		}
		layout->addWidget(diskButton);
	}
}

qulonglong CPanelWidget::currentItemHash() const
{
	QModelIndex currentIndex = _selectionModel->currentIndex();
	return hashByItemIndex(currentIndex);
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

void CPanelWidget::panelContentsChanged(Panel p , NavigationOperation operation)
{
	if (p == _panelPosition)
		fillFromPanel(_controller.panel(_panelPosition), operation);
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

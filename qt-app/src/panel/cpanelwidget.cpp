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
#include "cfilemanipulator.h"
#include "../cmainwindow.h"
#include "settings/csettings.h"
#include "settings.h"

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
#include <iostream>
#include <set>
#include <time.h>
#include <tuple>

CPanelWidget::CPanelWidget(QWidget *parent) :
	QWidget(parent),
	_filterDialog(this),
	ui(new Ui::CPanelWidget),
	_calcDirSizeShortcut(QKeySequence(Qt::Key_Space), this, SLOT(calcDirectorySize()), nullptr, Qt::WidgetWithChildrenShortcut),
	_selectCurrentItemShortcut(QKeySequence(Qt::Key_Insert), this, SLOT(invertCurrentItemSelection()), nullptr, Qt::WidgetWithChildrenShortcut),
	_showFilterEditorShortcut(QKeySequence("Ctrl+F"), this, SLOT(showFilterEditor()), nullptr, Qt::WidgetWithChildrenShortcut),
	_copyShortcut(QKeySequence("Ctrl+C"), this, SLOT(copySelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut),
	_cutShortcut(QKeySequence("Ctrl+X"), this, SLOT(cutSelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut),
	_pasteShortcut(QKeySequence("Ctrl+V"), this, SLOT(pasteSelectionFromClipboard()), nullptr, Qt::WidgetWithChildrenShortcut),
	_searchShortcut(QKeySequence("Alt+F7"), this, SLOT(openSearchWindow()), nullptr, Qt::WidgetWithChildrenShortcut)
{
	ui->setupUi(this);

	ui->_infoLabel->clear();
	ui->_driveInfoLabel->clear();

	ui->_pathNavigator->setLineEdit(new CLineEdit);
	ui->_pathNavigator->setHistoryMode(true);
	ui->_pathNavigator->installEventFilter(this);
	connect(ui->_pathNavigator, static_cast<void (CHistoryComboBox::*) (const QString&)>(&CHistoryComboBox::activated), this, &CPanelWidget::pathFromHistoryActivated);
	connect(ui->_pathNavigator, &CHistoryComboBox::itemActivated, this, &CPanelWidget::pathFromHistoryActivated);

	connect(ui->_list, &CFileListView::contextMenuRequested, this, &CPanelWidget::showContextMenuForItems);
	connect(ui->_list, &CFileListView::keyPressed, this, &CPanelWidget::fileListViewKeyPressed);

	connect(ui->_driveInfoLabel, &CClickableLabel::doubleClicked, this, &CPanelWidget::showFavoriteLocationsMenu);
	connect(ui->_btnFavs, &QPushButton::clicked, [&]{showFavoriteLocationsMenu(mapToGlobal(ui->_btnFavs->geometry().bottomLeft()));});
	connect(ui->_btnToRoot, &QToolButton::clicked, this, &CPanelWidget::toRoot);

	connect(&_filterDialog, &CFileListFilterDialog::filterTextChanged, this, &CPanelWidget::filterTextChanged);

	ui->_list->addEventObserver(this);

	onSettingsChanged();
}

CPanelWidget::~CPanelWidget()
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

void CPanelWidget::setPanelPosition(Panel p)
{
	assert_r(_panelPosition == UnknownPanel);
	_panelPosition = p;

	ui->_list->installEventFilter(this);
	ui->_list->viewport()->installEventFilter(this);
	ui->_list->setPanelPosition(p);

	_model = new(std::nothrow) CFileListModel(ui->_list, this);
	_model->setPanelPosition(p);
	connect(_model, &CFileListModel::itemEdited, this, &CPanelWidget::itemNameEdited);

	_sortModel = new(std::nothrow) CFileListSortFilterProxyModel(this);
	_sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	_sortModel->setFilterRole(FullNameRole);
	_sortModel->setPanelPosition(p);
	_sortModel->setSourceModel(_model);

	ui->_list->setModel(_sortModel);
	connect(_sortModel, &QSortFilterProxyModel::modelAboutToBeReset, ui->_list, &CFileListView::modelAboutToBeReset);
	connect(_sortModel, &CFileListSortFilterProxyModel::sorted, ui->_list, [=](){
		ui->_list->scrollTo(ui->_list->currentIndex());
	});

	_selectionModel = ui->_list->selectionModel(); // can only be called after setModel
	assert_r(_selectionModel);
	connect(_selectionModel, &QItemSelectionModel::selectionChanged, this, &CPanelWidget::selectionChanged);
	connect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged);

	_controller->setPanelContentsChangedListener(p, this);

	fillHistory();

	_controller->setVolumesChangedListener(this);
	_controller->panel(_panelPosition).addCurrentItemChangeListener(this);
}

// Returns the list of items added to the view
void CPanelWidget::fillFromList(const std::map<qulonglong, CFileSystemObject>& items, FileListRefreshCause operation)
{
// 	time_t start = clock();
// 	const auto globalStart = start;

	disconnect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged);

	const QString previousFolder = _directoryCurrentlyBeingDisplayed;
	const QModelIndex previousCurrentIndex = _selectionModel->currentIndex();

	ui->_list->saveHeaderState();
	_sortModel->setSourceModel(nullptr);
	_model->clear();

	_model->setColumnCount(NumberOfColumns);
	_model->setHorizontalHeaderLabels(QStringList() << tr("Name") << tr("Ext") << tr("Size") << tr("Date"));

	int itemRow = 0;

	// TODO: replace with a struct
	std::vector<std::tuple<int, FileListViewColumns, QStandardItem*>> qTreeViewItems;
	qTreeViewItems.reserve(items.size() * NumberOfColumns);

	for (const auto& item: items)
	{
		const CFileSystemObject& object = item.second;
		const auto& props = object.properties();

		std::cout << object.fullAbsolutePath().toLatin1().data();

		auto fileNameItem = new QStandardItem();
		fileNameItem->setEditable(false);
		if (props.type == Directory && props.type != Bundle)
			fileNameItem->setData(QString("[" % (object.isCdUp() ? QLatin1String("..") : props.fullName) % "]"), Qt::DisplayRole);
		else if (props.completeBaseName.isEmpty() && props.type == File) // File without a name, displaying extension in the name field and adding point to extension
			fileNameItem->setData(QString('.') + props.extension, Qt::DisplayRole);
		else
			fileNameItem->setData(props.completeBaseName, Qt::DisplayRole);
		fileNameItem->setIcon(object.icon());
		fileNameItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, NameColumn, fileNameItem);

		auto fileExtItem = new QStandardItem();
		fileExtItem->setEditable(false);
		if (!object.isCdUp() && !props.completeBaseName.isEmpty() && !props.extension.isEmpty())
			fileExtItem->setData(props.extension, Qt::DisplayRole);
		fileExtItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, ExtColumn, fileExtItem);

		auto sizeItem = new QStandardItem();
		sizeItem->setEditable(false);
		if (props.size > 0 || props.type == File)
			sizeItem->setData(fileSizeToString(props.size), Qt::DisplayRole);

		sizeItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, SizeColumn, sizeItem);

		auto dateItem = new QStandardItem();
		dateItem->setEditable(false);
		if (!object.isCdUp())
		{
			QDateTime modificationDate;
			modificationDate.setTime_t((uint) props.modificationDate);
			modificationDate = modificationDate.toLocalTime();
			dateItem->setData(modificationDate.toString("dd.MM.yyyy hh:mm"), Qt::DisplayRole);
		}
		dateItem->setData(props.hash, Qt::UserRole); // Unique identifier for this object;
		qTreeViewItems.emplace_back(itemRow, DateColumn, dateItem);

		++itemRow;
	}

	//qInfo () << __FUNCTION__ << "Creating" << items.size() << "items took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	//start = clock();
	for (const auto& qTreeViewItem: qTreeViewItems)
		_model->setItem(std::get<0>(qTreeViewItem), std::get<1>(qTreeViewItem), std::get<2>(qTreeViewItem));

	//qInfo () << __FUNCTION__ << "Setting" << items.size() << "items to the model took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	//start = clock();
	_sortModel->setSourceModel(_model);
	//qInfo () << __FUNCTION__ << "Setting the source model to sort model took" << (clock() - start) * 1000 / CLOCKS_PER_SEC << "ms";

	ui->_list->restoreHeaderState();

	auto indexUnderCursor = _sortModel->index(0, 0);

	// Setting the cursor position as appropriate
	if (operation == refreshCauseCdUp)
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
			indexUnderCursor = indexByHash(targetFolderHash);
	}
	else if (operation != refreshCauseForwardNavigation || CSettings().value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS).toBool())
	{
		const qulonglong itemHashToSetCursorTo = _controller->currentItemHashForFolder(_panelPosition, _controller->panel(_panelPosition).currentDirPathPosix());
		const QModelIndex itemIndexToSetCursorTo = indexByHash(itemHashToSetCursorTo, true);
		if (itemIndexToSetCursorTo.isValid())
			indexUnderCursor = itemIndexToSetCursorTo;
		else if (previousCurrentIndex.isValid())
			indexUnderCursor = _sortModel->index(std::min(previousCurrentIndex.row(), _sortModel->rowCount() - 1), 0);
	}

	ui->_list->moveCursorToItem(indexUnderCursor);

	connect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged);
	currentItemChanged(_selectionModel->currentIndex(), QModelIndex());
	selectionChanged(QItemSelection(), QItemSelection());

	//qInfo () << __FUNCTION__ << items.size() << "items," << (clock() - globalStart) * 1000 / CLOCKS_PER_SEC << "ms";
}

void CPanelWidget::fillFromPanel(const CPanel &panel, FileListRefreshCause operation)
{
	const auto itemList = panel.list();
	const auto previousSelection = selectedItemsHashes(true);
	std::set<qulonglong> selectedItemsHashes; // For fast search
	for (const auto slectedItemHash: previousSelection)
		selectedItemsHashes.insert(slectedItemHash);

	fillFromList(itemList, operation);
	_directoryCurrentlyBeingDisplayed = panel.currentDirPathPosix();

	// Restoring previous selection
	if (!selectedItemsHashes.empty())
	{
		CTimeElapsed timer(true);
		QItemSelection selection;
		for (int row = 0, numRows = _sortModel->rowCount(); row < numRows; ++row)
		{
			const qulonglong hash = hashBySortModelIndex(_sortModel->index(row, 0));
			if (selectedItemsHashes.count(hash) != 0)
				selection.select(_sortModel->index(row, 0), _sortModel->index(row, 0));
		}

		timer.start();
		if (!selection.empty())
			_selectionModel->select(selection, QItemSelectionModel::Rows | QItemSelectionModel::Select);

		qInfo() << "_selectionModel->select took" << timer.elapsed() << "ms for" << selection.size() << "items";
	}

	fillHistory();
	updateCurrentDiskButton();
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

	OsShell::openShellContextMenuForObjects(paths, pos.x(), pos.y(), (void*)winId());
}

void CPanelWidget::showContextMenuForDisk(QPoint pos)
{
#ifdef _WIN32
	const auto button = dynamic_cast<const QPushButton*>(sender());
	if (!button)
		return;

	pos = button->mapToGlobal(pos);
	const size_t diskId = (size_t)(button->property("id").toULongLong());
	std::vector<std::wstring> diskPath(1, _controller->volumePath(diskId).toStdWString());
	OsShell::openShellContextMenuForObjects(diskPath, pos.x(), pos.y(), (HWND)winId());
#else
	Q_UNUSED(pos);
#endif
}

void CPanelWidget::calcDirectorySize()
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

	const size_t id = (size_t)(sender()->property("id").toULongLong());
	if (!_controller->switchToVolume(_panelPosition, id))
		QMessageBox::information(this, tr("Failed to switch disk"), tr("The disk %1 is inaccessible (locked or doesn't exist).").arg(_controller->volumePath(id)));

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
	assert_and_return_r(newCurrentIndex.isValid(), );

	_selectionModel->setCurrentIndex(newCurrentIndex, QItemSelectionModel::Current | QItemSelectionModel::Rows);
}

void CPanelWidget::itemNameEdited(qulonglong hash, QString newName)
{
	CFileSystemObject item = _controller->itemByHash(_panelPosition, hash);
	if (item.isCdUp())
		return;

	assert_r(item.parentDirPath().endsWith('/'));

	CFileManipulator itemManipulator(item);
	const auto result = itemManipulator.moveAtomically(item.parentDirPath(), newName);

	// This is required for the UI to know to move the cursor to the renamed item
	if (result == FileOperationResultCode::Ok)
		_controller->setCursorPositionForCurrentFolder(_panelPosition, CFileSystemObject(item.parentDirPath() + newName).hash());

	if (result == FileOperationResultCode::TargetAlreadyExists)
	{
		const auto text = item.isFile() ? tr("The file %1 already exists.") : tr("The folder %1 already exists.");
		QMessageBox::information(this, tr("Item already exists"), text.arg(newName));
	}
	else if (result != FileOperationResultCode::Ok)
	{
		QString errorMessage = tr("Failed to rename %1 to %2").arg(item.fullName(), newName);
		if (!itemManipulator.lastErrorMessage().isEmpty())
			errorMessage.append(":\n" % itemManipulator.lastErrorMessage() % '.');

		QMessageBox::critical(this, tr("Renaming failed"), errorMessage);
	}
}

void CPanelWidget::toRoot()
{
	if (!_currentDisk.isEmpty())
		_controller->setPath(_panelPosition, _currentDisk, refreshCauseOther);
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
				const QString& path = toPosixSeparators(item.absolutePath);
				if (CFileSystemObject(path) == CFileSystemObject(currentDirPathNative()))
				{
					action->setCheckable(true);
					action->setChecked(true);
				}

				QObject::connect(action, &QAction::triggered, [this, path](){
					_controller->setPath(_panelPosition, path, refreshCauseOther);
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
				_controller->favoriteLocations().addItem(locations, name);
			}
		});
	};

	createMenus(&menu, _controller->favoriteLocations().locations());
	menu.addSeparator();
	QAction * edit = menu.addAction(tr("Edit..."));
	connect(edit, &QAction::triggered, this, &CPanelWidget::showFavoriteLocationsEditor);
	const QAction* action = menu.exec(pos);
	if (action) // Something was selected
		setFocusToFileList(); // #84
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
		_controller->navigateUp(_panelPosition);
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
	const auto hashes = selectedItemsHashes();
	std::vector<std::wstring> paths;
	paths.reserve(hashes.size());
	for (auto hash: hashes)
		paths.emplace_back(_controller->itemByHash(_panelPosition, hash).fullAbsolutePath().toStdWString());

	OsShell::copyObjectsToClipboard(paths, (void*)winId());
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

	OsShell::cutObjectsToClipboard(paths, (void*)winId());
#endif
}

void CPanelWidget::pasteSelectionFromClipboard()
{
	QClipboard * clipBoard = QApplication::clipboard();
	// If the clipboard contains an image (not a file), paste it into a file
	if (clipBoard && clipBoard->mimeData()->hasImage())
	{
		QImage image = qvariant_cast<QImage>(clipBoard->mimeData()->imageData());
		if (!image.isNull())
			assert_r(pasteImage(image));
	}

#ifndef _WIN32
	if (clipBoard)
	{
		const QMimeData * data = clipBoard->mimeData();
		_model->dropMimeData(clipBoard->mimeData(), (data && data->property("cut").toBool()) ? Qt::MoveAction : Qt::CopyAction, 0, 0, QModelIndex());
	}
#else
	auto hwnd = (void*)winId();
	const auto currentDirWString = currentDirPathNative().toStdWString();
	_controller->execOnWorkerThread([=]() {
		OsShell::pasteFilesAndFoldersFromClipboard(currentDirWString, hwnd);
	});
#endif
}

void CPanelWidget::pathFromHistoryActivated(QString path)
{
	const CFileSystemObject processedPath(path); // Needed for expanding environment variables in the path
	ui->_list->setFocus();
	if (_controller->setPath(_panelPosition, processedPath.fullAbsolutePath(), refreshCauseOther) == FileOperationResultCode::DirNotAccessible)
		QMessageBox::information(this, tr("Failed to set the path"), tr("The path %1 is inaccessible (locked or doesn't exist). Setting the closest accessible path instead.").arg(path));
}

void CPanelWidget::openSearchWindow()
{

}

void CPanelWidget::fillHistory()
{
	const auto& history = _controller->panel(_panelPosition).history();
	if (history.empty())
		return;

	ui->_pathNavigator->clear();
	for(auto it = history.rbegin(); it != history.rend(); ++it)
		ui->_pathNavigator->addItem(toNativeSeparators(it->endsWith('/') ? *it : (*it) + "/"));

	ui->_pathNavigator->setCurrentIndex(static_cast<int>(history.size() - 1 - history.currentIndex()));
}

void CPanelWidget::updateInfoLabel(const std::vector<qulonglong>& selection)
{
	uint64_t numFilesSelected = 0, numFoldersSelected = 0, totalSize = 0, sizeSelected = 0, totalNumFolders = 0, totalNumFiles = 0;
	for (const auto& item: _controller->panel(_panelPosition).list())
	{
		const CFileSystemObject& object = item.second;
		if (object.isCdUp())
			continue;

		if (object.isFile())
			++totalNumFiles;
		else if (object.isDir())
			++totalNumFolders;

		totalSize += object.size();
	}

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

	ui->_infoLabel->setText(tr("%1/%2 files, %3/%4 folders selected (%5 / %6)").arg(numFilesSelected).arg(totalNumFiles).
		arg(numFoldersSelected).arg(totalNumFolders).
		arg(fileSizeToString(sizeSelected), fileSizeToString(totalSize)));
}

bool CPanelWidget::fileListReturnPressOrDoubleClickPerformed(const QModelIndex& item)
{
	assert_r(item.isValid());
	const QModelIndex source = _sortModel->mapToSource(item);
	const qulonglong hash = _model->item(source.row(), source.column())->data(Qt::UserRole).toULongLong();
	emit itemActivated(hash, this);
	return true; // Consuming the event
}

void CPanelWidget::volumesChanged(const std::deque<VolumeInfo>& drives, Panel p)
{
	if (p != _panelPosition)
		return;

	_currentDisk.clear();

	if (!ui->_driveButtonsWidget->layout())
	{
		auto flowLayout = new QFlowLayout(ui->_driveButtonsWidget, 0, 0, 0);
		flowLayout->setSpacing(1);
		ui->_driveButtonsWidget->setLayout(flowLayout);
	}

	// Clearing and deleting the previous buttons
	QLayout * layout = ui->_driveButtonsWidget->layout();
	assert_r(layout);
	while (layout->count() > 0)
	{
		QWidget * w = layout->itemAt(0)->widget();
		layout->removeWidget(w);
		w->deleteLater();
	}

	// Creating and adding new buttons
	for (size_t i = 0, n = drives.size(); i < n; ++i)
	{
		const auto& driveInfo = drives[i];
		if (!driveInfo.isReady || !driveInfo.rootObjectInfo.isValid())
			continue;

#ifdef _WIN32
		const QString name = driveInfo.rootObjectInfo.fullAbsolutePath().remove(":/");
#else
		const QString name = driveInfo.volumeLabel;
#endif

		assert_r(layout);
		auto diskButton = new QPushButton;
		diskButton->setCheckable(true);
		diskButton->setIcon(drives[i].rootObjectInfo.icon());
		diskButton->setText(name);
		diskButton->setFixedWidth(QFontMetrics(diskButton->font()).width(diskButton->text()) + 5 + diskButton->iconSize().width() + 20);
		diskButton->setProperty("id", (qulonglong)i);
		diskButton->setContextMenuPolicy(Qt::CustomContextMenu);
		diskButton->setToolTip(driveInfo.volumeLabel);
		connect(diskButton, &QPushButton::clicked, this, &CPanelWidget::driveButtonClicked);
		connect(diskButton, &QPushButton::customContextMenuRequested, this, &CPanelWidget::showContextMenuForDisk);
		layout->addWidget(diskButton);
	}

	updateCurrentDiskButton();
}

qulonglong CPanelWidget::hashBySortModelIndex(const QModelIndex &index) const
{
	if (!index.isValid())
		return 0;
	QStandardItem * item = _model->item(_sortModel->mapToSource(index).row(), 0);
	assert_r(item);
	bool ok = false;
	const qulonglong hash = item->data(Qt::UserRole).toULongLong(&ok);
	assert_r(ok);
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
		auto wEvent = static_cast<QWheelEvent*>(e);
		if (wEvent && wEvent->modifiers() == Qt::ShiftModifier)
		{
			if (wEvent->delta() > 0)
				_controller->navigateBack(_panelPosition);
			else
				_controller->navigateForward(_panelPosition);
			return true;
		}
	}
	else if (object == ui->_pathNavigator && e->type() == QEvent::KeyPress)
	{
		auto keyEvent = static_cast<QKeyEvent*>(e);
		if (keyEvent->key() == Qt::Key_Escape)
		{
			ui->_pathNavigator->resetToLastSelected(false);
			ui->_list->setFocus();
		}
	}

	return QWidget::eventFilter(object, e);
}

void CPanelWidget::panelContentsChanged(Panel p , FileListRefreshCause operation)
{
	if (p == _panelPosition)
		fillFromPanel(_controller->panel(_panelPosition), operation);
}

void CPanelWidget::itemDiscoveryInProgress(Panel p, qulonglong /*itemHash*/, size_t /*progress*/, const QString& /*currentDir*/)
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
		for (const auto selectedItem: selection)
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

void CPanelWidget::onSettingsChanged()
{
	QFont font;
	font.fromString(CSettings().value(KEY_INTERFACE_FILE_LIST_FONT, INTERFACE_FILE_LIST_FONT_DEFAULT).toString());
	ui->_list->setFont(font);
}

void CPanelWidget::updateCurrentDiskButton()
{
	QLayout * layout = ui->_driveButtonsWidget->layout();
	if (!layout)
		return;

	for (int i = 0; i < layout->count(); ++i)
	{
		QLayoutItem* item = layout->itemAt(i);
		auto button = dynamic_cast<QPushButton*>(item ? item->widget() : nullptr);
		if (!button)
			continue;

		const size_t id = (size_t)(button->property("id").toULongLong());
		const size_t currentDriveId = _controller->currentVolumeIndex(_panelPosition);
		if (id == currentDriveId)
		{
			button->setChecked(true);
			const auto diskInfo = _controller->volumeEnumerator().drives()[id];
			_currentDisk = diskInfo.rootObjectInfo.fullAbsolutePath();
			ui->_driveInfoLabel->setText(tr("%1 (%2): <b>%4 free</b> of %5 total").
				arg(diskInfo.volumeLabel, diskInfo.fileSystemName, fileSizeToString(diskInfo.freeSize, 'M', " "), fileSizeToString(diskInfo.volumeSize, 'M', " ")));

			return;
		}
	}
}

bool CPanelWidget::pasteImage(const QImage& image)
{
	const QString currentDirPath = currentDirPathNative();
	assert(currentDirPath.endsWith(nativeSeparator()));

	QString imagePath = currentDirPath + "clipboard.png";
	for (int i = 1; CFileSystemObject{ imagePath }.exists(); ++i)
	{
		imagePath = currentDirPath + "clipboard(" + QString::number(i) + ").png";
	}

	return image.save(imagePath, "png");
}

#include "cpanelwidget.h"
#include "filelistwidget/cfilelistview.h"
#include "filelistwidget/model/cfilelistmodel.h"
#include "qflowlayout.h"
#include "shell/cshell.h"
#include "columns.h"
#include "filelistwidget/model/cfilelistsortfilterproxymodel.h"
#include "pluginengine/cpluginengine.h"
#include "../favoritelocationseditor/cfavoritelocationseditor.h"
#include "iconprovider/ciconprovider.h"
#include "filesystemhelperfunctions.h"
#include "filesystemhelpers/filesystemhelpers.hpp"
#include "cfilemanipulator.h"
#include "settings/csettings.h"
#include "settings.h"
#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "widgets/clineedit.h"

#include "system/ctimeelapsed.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cpanelwidget.h"

#include <QClipboard>
#include <QCompleter>
#include <QDebug>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <assert.h>
#include <set>

CPanelWidget::CPanelWidget(QWidget *parent) noexcept :
	QWidget(parent),
	_filterDialog(this),
	ui(new Ui::CPanelWidget),
	_calcDirSizeShortcut(QKeySequence(Qt::Key_Space), this, SLOT(calcDirectorySize()), nullptr, Qt::WidgetWithChildrenShortcut),
	_selectCurrentItemShortcut(QKeySequence(Qt::Key_Insert), this, SLOT(invertCurrentItemSelection()), nullptr, Qt::WidgetWithChildrenShortcut),
	_copyShortcut(QKeySequence(QSL("Ctrl+C")), this, SLOT(copySelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut),
	_cutShortcut(QKeySequence(QSL("Ctrl+X")), this, SLOT(cutSelectionToClipboard()), nullptr, Qt::WidgetWithChildrenShortcut),
	_pasteShortcut(QKeySequence(QSL("Ctrl+V")), this, SLOT(pasteSelectionFromClipboard()), nullptr, Qt::WidgetWithChildrenShortcut)
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

	assert_r(connect(ui->_driveInfoLabel, &CClickableLabel::doubleClicked, this, &CPanelWidget::showFavoriteLocationsMenu));
	assert_r(connect(ui->_btnFavs, &QPushButton::clicked, this, [&]{showFavoriteLocationsMenu(mapToGlobal(ui->_btnFavs->geometry().bottomLeft()));}));
	assert_r(connect(ui->_btnToRoot, &QToolButton::clicked, this, &CPanelWidget::toRoot));

	assert_r(connect(&_filterDialog, &CFileListFilterDialog::filterTextChanged, this, &CPanelWidget::filterTextChanged));

	ui->_list->addEventObserver(this);

	onSettingsChanged();
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
	assert_r(connect(_model, &CFileListModel::itemEdited, this, &CPanelWidget::itemNameEdited));

	_sortModel = new(std::nothrow) CFileListSortFilterProxyModel(this);
	_sortModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	_sortModel->setFilterRole(FullNameRole);
	_sortModel->setPanelPosition(p);
	_sortModel->setSourceModel(_model);

	ui->_list->setModel(_sortModel);
	assert_r(connect(_sortModel, &QSortFilterProxyModel::modelAboutToBeReset, ui->_list, &CFileListView::modelAboutToBeReset));
	assert_r(connect(_sortModel, &CFileListSortFilterProxyModel::sorted, ui->_list, [this](){
		ui->_list->scrollTo(ui->_list->currentIndex());
	}));

	_selectionModel = ui->_list->selectionModel(); // can only be called after setModel
	assert_r(_selectionModel);
	assert_r(connect(_selectionModel, &QItemSelectionModel::selectionChanged, this, &CPanelWidget::selectionChanged));
	assert_r(connect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged));

	_controller->setPanelContentsChangedListener(p, this);

	_controller->setVolumesChangedListener(this);
	_controller->panel(_panelPosition).addCurrentItemChangeListener(this);
}

// Returns the list of items added to the view
void CPanelWidget::fillFromList(const std::map<qulonglong, CFileSystemObject>& items, FileListRefreshCause operation)
{
	CTimeElapsed timer{ true };

	disconnect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged);

	const QString previousFolder = _directoryCurrentlyBeingDisplayed;
	const QModelIndex previousCurrentIndex = _selectionModel->currentIndex();

	ui->_list->saveHeaderState();
	_sortModel->setSourceModel(nullptr);
	_model->clear();

	_model->setColumnCount(NumberOfColumns);
	_model->setHorizontalHeaderLabels(QStringList{ tr("Name"), tr("Ext"), tr("Size"), tr("Date") });

	int itemRow = 0;

	struct TreeViewItem {
		const int row;
		const FileListViewColumn column;
		QStandardItem* const item;
	};

	std::vector<TreeViewItem> qTreeViewItems;
	qTreeViewItems.reserve(items.size() * NumberOfColumns);

	const bool useLessPreciseIcons = items.size() > 3000;
	for (const auto& item: items)
	{
		const CFileSystemObject& object = item.second;
		const auto& props = object.properties();

		auto* fileNameItem = new QStandardItem();
		fileNameItem->setEditable(false);
		if (props.type == Directory)
			fileNameItem->setData(QString("[" % (object.isCdUp() ? QLatin1String("..") : props.fullName) % "]"), Qt::DisplayRole);
		else if (props.completeBaseName.isEmpty() && props.type == File) // File without a name, displaying extension in the name field and adding point to extension
			fileNameItem->setData(QString('.') + props.extension, Qt::DisplayRole);
		else
			fileNameItem->setData(props.completeBaseName, Qt::DisplayRole);
		fileNameItem->setIcon(CIconProvider::iconForFilesystemObject(object, useLessPreciseIcons));
		fileNameItem->setData(static_cast<qulonglong>(props.hash), Qt::UserRole); // Unique identifier for this object
		qTreeViewItems.emplace_back(TreeViewItem{ itemRow, NameColumn, fileNameItem });

		auto* fileExtItem = new QStandardItem();
		fileExtItem->setEditable(false);
		if (!object.isCdUp() && !props.completeBaseName.isEmpty() && !props.extension.isEmpty())
			fileExtItem->setData(props.extension, Qt::DisplayRole);
		fileExtItem->setData(static_cast<qulonglong>(props.hash), Qt::UserRole); // Unique identifier for this object
		qTreeViewItems.emplace_back(TreeViewItem{ itemRow, ExtColumn, fileExtItem });

		auto* sizeItem = new QStandardItem();
		sizeItem->setEditable(false);
		if (props.size > 0 || props.type == File)
			sizeItem->setData(fileSizeToString(props.size), Qt::DisplayRole);

		sizeItem->setData(static_cast<qulonglong>(props.hash), Qt::UserRole); // Unique identifier for this object
		qTreeViewItems.emplace_back(TreeViewItem{ itemRow, SizeColumn, sizeItem });

		auto* dateItem = new QStandardItem();
		dateItem->setEditable(false);
		if (!object.isCdUp())
		{
			QDateTime modificationDate = fromTime_t(props.modificationDate).toLocalTime();
			dateItem->setData(modificationDate.toString(QSL("dd.MM.yyyy hh:mm:ss")), Qt::DisplayRole);
		}
		dateItem->setData(static_cast<qulonglong>(props.hash), Qt::UserRole); // Unique identifier for this object
		qTreeViewItems.emplace_back(TreeViewItem{ itemRow, DateColumn, dateItem });

		++itemRow;
	}

	for (const auto& qTreeViewItem: qTreeViewItems)
		_model->setItem(qTreeViewItem.row, qTreeViewItem.column, qTreeViewItem.item);

	_sortModel->setSourceModel(_model);

	ui->_list->restoreHeaderState();

	auto indexUnderCursor = _sortModel->index(0, 0);

	// Setting the cursor position as appropriate
	if (operation == refreshCauseCdUp)
	{
		// Setting the folder we've just stepped out of as current
		qulonglong targetFolderHash = 0;
		for (const auto& item: items)
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

	assert_r(connect(_selectionModel, &QItemSelectionModel::currentChanged, this, &CPanelWidget::currentItemChanged));
	currentItemChanged(_selectionModel->currentIndex(), QModelIndex());
	selectionChanged(QItemSelection(), QItemSelection());

	if (items.size() > 1000)
		qInfo() << __FUNCTION__ << "Procesing" << items.size() << "items took" << timer.elapsed() << "ms";
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
			if (selectedItemsHashes.contains(hash))
				selection.select(_sortModel->index(row, 0), _sortModel->index(row, 0));
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

void CPanelWidget::itemNameEdited(qulonglong hash, QString newName)
{
	CFileSystemObject item = _controller->itemByHash(_panelPosition, hash);
	if (item.isCdUp())
		return;

	assert_r(item.parentDirPath().endsWith('/'));
	newName = FileSystemHelpers::trimUnsupportedSymbols(newName);

	CFileManipulator itemManipulator(item);
	auto result = itemManipulator.moveAtomically(item.parentDirPath(), newName);

	if (result == FileOperationResultCode::TargetAlreadyExists)
	{
		if (item.isDir())
			QMessageBox::information(this, tr("Item already exists"), tr("The folder %1 already exists.").arg(newName));
		else if (item.isFile())
		{
			// Fix for https://github.com/VioletGiraffe/file-commander/issues/123
			if (QMessageBox::question(
					this,
					tr("Item already exists"),
					tr("The file %1 already exists, do you want to overwrite it?").arg(newName),
					QMessageBox::Yes | QMessageBox::No,
					QMessageBox::Yes
				)
				== QMessageBox::Yes)
			{
				result = itemManipulator.moveAtomically(item.parentDirPath(), newName, OverwriteExistingFile{ true });
			}
		}

	}

	if (result == FileOperationResultCode::Ok)
	{
		// This is required for the UI to know to move the cursor to the renamed item
		_controller->setCursorPositionForCurrentFolder(_panelPosition, CFileSystemObject(item.parentDirPath() + newName).hash());
	}
	else
	{
		QString errorMessage = tr("Failed to rename %1 to %2").arg(item.fullName(), newName);
		if (!itemManipulator.lastErrorMessage().isEmpty())
			errorMessage.append(":\n" % itemManipulator.lastErrorMessage() % '.');

		QMessageBox::critical(this, tr("Renaming failed"), errorMessage);
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
	_filterDialog.showAt(ui->_list->geometry().bottomLeft());
}

void CPanelWidget::filterTextChanged(const QString& filterText)
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
	auto* hwnd = reinterpret_cast<void*>(winId());
	const auto currentDirWString = currentDirPathNative().toStdWString();
	_controller->execOnWorkerThread([hwnd, currentDirWString]() {
		OsShell::pasteFilesAndFoldersFromClipboard(currentDirWString, hwnd);
	});
#endif
}

void CPanelWidget::pathFromHistoryActivated(const QString& path)
{
	const CFileSystemObject processedPath(path); // Needed for expanding environment variables in the path
	ui->_list->setFocus();
	if (_controller->setPath(_panelPosition, processedPath.fullAbsolutePath(), refreshCauseOther) == FileOperationResultCode::DirNotAccessible)
		QMessageBox::information(this, tr("Failed to set the path"), tr("The path %1 is inaccessible (locked or doesn't exist). Setting the closest accessible path instead.").arg(path));
}

void CPanelWidget::fillHistory()
{
	const auto& history = _controller->panel(_panelPosition).history();
	if (history.empty())
		return;

	ui->_pathNavigator->clear();

	QStringList items;
	items.reserve((QStringList::size_type)history.size());
	for (auto it = history.rbegin(); it != history.rend(); ++it)
		items.push_back(toNativeSeparators(it->endsWith('/') ? *it : (*it) + '/'));

	ui->_pathNavigator->addItems(items);
	ui->_pathNavigator->setCurrentIndex(static_cast<int>(history.size() - 1 - history.currentIndex()));
}

void CPanelWidget::updateInfoLabel(const std::vector<qulonglong>& selection)
{
	uint64_t numFilesSelected = 0;
	uint64_t numFoldersSelected = 0;
	uint64_t totalSize = 0;
	uint64_t sizeSelected = 0;
	uint64_t totalNumFolders = 0;
	uint64_t totalNumFiles = 0;

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

void CPanelWidget::volumesChanged(const std::vector<VolumeInfo>& drives, Panel p, bool drivesListOrReadinessChanged) noexcept
{
	if (p != _panelPosition)
		return;

	_currentVoumePath.clear();

	if (!ui->_driveButtonsWidget->layout())
	{
#ifdef _WIN32
		auto* flowLayout = new(std::nothrow) FlowLayout(ui->_driveButtonsWidget, 0, 0, 0);
#else
		auto* flowLayout = new(std::nothrow) FlowLayout(ui->_driveButtonsWidget, 0, 5, 5);
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
		result.reserve(selection.size());
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
	for (int i = 0, n = layout->count(); i < n; ++i)
	{
		auto* button = dynamic_cast<QPushButton*>(layout->itemAt(i)->widget());
		if (!button)
			continue;

		const auto id = button->property("id").toULongLong();
		button->setChecked(currentVolumeInfo ? (id == currentVolumeInfo->id()) : false); // Need to clear all selection if the current volume cannot be determined
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

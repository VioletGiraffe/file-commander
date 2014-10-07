#include "progressdialogs/ccopymovedialog.h"
#include "progressdialogs/cdeleteprogressdialog.h"
#include "progressdialogs/cfileoperationconfirmationprompt.h"
#include "cmainwindow.h"
#include "ui_cmainwindow.h"
#include "settings.h"
#include "settings/csettings.h"
#include "shell/cshell.h"
#include "settingsui/csettingsdialog.h"
#include "settings/csettingspageinterface.h"
#include "settings/csettingspageoperations.h"
#include "settings/csettingspageedit.h"
#include "settings/csettingspageother.h"
#include "pluginengine/cpluginengine.h"
#include "panel/filelistwidget/cfilelistview.h"
#include "panel/columns.h"

#include <assert.h>

#ifdef _WIN32
#include <Windows.h>
#endif

// Main window settings keys
#define KEY_RPANEL_STATE      "Ui/RPanel/State"
#define KEY_LPANEL_STATE      "Ui/LPanel/State"
#define KEY_RPANEL_GEOMETRY   "Ui/RPanel/Geometry"
#define KEY_LPANEL_GEOMETRY   "Ui/LPanel/Geometry"
#define KEY_GEOMETRY          "Ui/Geometry"
#define KEY_STATE             "Ui/State"
#define KEY_SPLITTER_SIZES    "Ui/Splitter"
#define KEY_LAST_ACTIVE_PANEL "Ui/LastActivePanel"

CMainWindow * CMainWindow::_instance = 0;

CMainWindow::CMainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::CMainWindow),
	_controller(std::make_shared<CController>()),
	_currentFileList(0),
	_otherFileList(0),
	_quickViewActive(false)
{
	assert(!_instance);
	_instance = this;
	ui->setupUi(this);

	connect(qApp, SIGNAL(focusChanged(QWidget *, QWidget *)), SLOT(focusChanged(QWidget*,QWidget*)));

	_controller->pluginProxy().setToolMenuEntryCreatorImplementation(CPluginProxy::CreateToolMenuEntryImplementationType(std::bind(&CMainWindow::createToolMenuEntries, this, std::placeholders::_1)));

	_currentFileList = ui->leftPanel;
	_otherFileList   = ui->rightPanel;

	connect(ui->leftPanel->fileListView(), SIGNAL(ctrlEnterPressed()), SLOT(pasteCurrentFileName()));
	connect(ui->rightPanel->fileListView(), SIGNAL(ctrlEnterPressed()), SLOT(pasteCurrentFileName()));
	connect(ui->leftPanel->fileListView(), SIGNAL(ctrlShiftEnterPressed()), SLOT(pasteCurrentFilePath()));
	connect(ui->rightPanel->fileListView(), SIGNAL(ctrlShiftEnterPressed()), SLOT(pasteCurrentFilePath()));

	connect(ui->leftPanel, SIGNAL(currentItemChanged(Panel,qulonglong)), SLOT(currentItemChanged(Panel,qulonglong)));
	connect(ui->rightPanel, SIGNAL(currentItemChanged(Panel,qulonglong)), SLOT(currentItemChanged(Panel,qulonglong)));

	connect(ui->leftPanel, SIGNAL(itemActivated(qulonglong,CPanelWidget*)), SLOT(itemActivated(qulonglong,CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(itemActivated(qulonglong,CPanelWidget*)), SLOT(itemActivated(qulonglong,CPanelWidget*)));

	ui->leftPanel->fileListView()->addEventObserver(this);
	ui->rightPanel->fileListView()->addEventObserver(this);

	initButtons();
	initActions();

	ui->leftPanel->setPanelPosition(LeftPanel);
	ui->rightPanel->setPanelPosition(RightPanel);

	ui->fullPath->clear();

	QSplitterHandle * handle = ui->splitter->handle(1);
	handle->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(handle, SIGNAL(customContextMenuRequested(QPoint)), SLOT(splitterContextMenuRequested(QPoint)));

	connect(ui->commandLine, SIGNAL(itemActivated(QString)), SLOT(executeCommand(QString)));

	_commandLineCompleter.setCaseSensitivity(Qt::CaseInsensitive);
	_commandLineCompleter.setCompletionMode(QCompleter::InlineCompletion);
	_commandLineCompleter.setCompletionColumn(NameColumn);
	ui->commandLine->setCompleter(&_commandLineCompleter);
	ui->commandLine->setClearEditorOnItemActivation(true);

	ui->leftWidget->setCurrentIndex(0); // PanelWidget
	ui->rightWidget->setCurrentIndex(0); // PanelWidget
}

void CMainWindow::initButtons()
{
	connect(ui->btnView, SIGNAL(clicked()), SLOT(viewFile()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F3"), this, SLOT(viewFile()), 0, Qt::WidgetWithChildrenShortcut)));

	connect(ui->btnEdit, SIGNAL(clicked()), SLOT(editFile()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F4"), this, SLOT(editFile()), 0, Qt::WidgetWithChildrenShortcut)));

	connect(ui->btnCopy, SIGNAL(clicked()), SLOT(copySelectedFiles()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F5"), this, SLOT(copySelectedFiles()), 0, Qt::WidgetWithChildrenShortcut)));

	connect(ui->btnMove, SIGNAL(clicked()), SLOT(moveSelectedFiles()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F6"), this, SLOT(moveSelectedFiles()), 0, Qt::WidgetWithChildrenShortcut)));

	connect(ui->btnNewFolder, SIGNAL(clicked()), SLOT(createFolder()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F7"), this, SLOT(createFolder()), 0, Qt::WidgetWithChildrenShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+F7"), this, SLOT(createFile()), 0, Qt::WidgetWithChildrenShortcut)));

	connect(ui->btnDelete, SIGNAL(clicked()), SLOT(deleteFiles()));
	connect(ui->btnDelete, SIGNAL(customContextMenuRequested(QPoint)), SLOT(showRecycleBInContextMenu(QPoint)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F8"), this, SLOT(deleteFiles()), 0, Qt::WidgetWithChildrenShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Delete"), this, SLOT(deleteFiles()), 0, Qt::WidgetWithChildrenShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+F8"), this, SLOT(deleteFilesIrrevocably()), 0, Qt::WidgetWithChildrenShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+Delete"), this, SLOT(deleteFilesIrrevocably()), 0, Qt::WidgetWithChildrenShortcut)));

	// Command line
	ui->commandLine->setSelectPreviousItemShortcut(QKeySequence("Ctrl+E"));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+E"), this, SLOT(selectPreviousCommandInTheCommandLine()), 0, Qt::WidgetWithChildrenShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Esc"), this, SLOT(clearCommandLineAndRestoreFocus()), 0, Qt::WidgetWithChildrenShortcut)));
}

void CMainWindow::initActions()
{
	connect(ui->actionRefresh, SIGNAL(triggered()), SLOT(refresh()));

	connect(ui->actionOpen_Console_Here, SIGNAL(triggered()), SLOT(openTerminal()));
	connect(ui->actionExit, SIGNAL(triggered()), qApp, SLOT(quit()));

	ui->action_Show_hidden_files->setChecked(CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool());
	connect(ui->action_Show_hidden_files, SIGNAL(triggered()), SLOT(showHiddenFiles()));
	connect(ui->actionShowAllFiles, SIGNAL(triggered()), SLOT(showAllFilesFromCurrentFolderAndBelow()));
	connect(ui->action_Settings, SIGNAL(triggered()), SLOT(openSettingsDialog()));
	connect(ui->actionCalculate_occupied_space, SIGNAL(triggered()), SLOT(calculateOccupiedSpace()));
	connect(ui->actionQuick_view, SIGNAL(triggered()), SLOT(toggleQuickView()));
}

// For manual focus management
void CMainWindow::tabKeyPressed()
{
	_otherFileList->setFocusToFileList();
}

bool CMainWindow::copyFiles(const std::vector<CFileSystemObject> & files, const QString & destDir)
{
	if (files.empty() || destDir.isEmpty())
		return false;

	const QString destPath = files.size() == 1 && files.front().isFile() ? cleanPath(destDir + toNativeSeparators("/") + files.front().fullName()) : destDir;
	CFileOperationConfirmationPrompt prompt("Copy files", QString("Copy %1 %2 to").arg(files.size()).arg(files.size() > 1 ? "files" : "file"), destPath, this);
	if (CSettings().value(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, true).toBool())
	{
		if (prompt.exec() != QDialog::Accepted)
			return false;
	}

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationCopy, files, prompt.text(), this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->connect(dialog, SIGNAL(closed()), SLOT(deleteLater()));
	dialog->show();

	return true;
}

bool CMainWindow::moveFiles(const std::vector<CFileSystemObject> & files, const QString & destDir)
{
	if (files.empty() || destDir.isEmpty())
		return false;

	if (CSettings().value(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, true).toBool())
	{
		CFileOperationConfirmationPrompt prompt("Move files", QString("Move %1 %2 to").arg(files.size()).arg(files.size() > 1 ? "files" : "file"), destDir, this);
		if (prompt.exec() != QDialog::Accepted)
			return false;
	}

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationMove, files, destDir, this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->connect(dialog, SIGNAL(closed()), SLOT(deleteLater()));
	dialog->show();

	return true;
}


CMainWindow::~CMainWindow()
{
	_instance = this;
	delete ui;
}

CMainWindow *CMainWindow::get()
{
	return _instance;
}

void CMainWindow::updateInterface()
{
	CSettings s;
	restoreGeometry(s.value(KEY_GEOMETRY).toByteArray());
	restoreState(s.value(KEY_STATE).toByteArray());
	ui->splitter->restoreState(s.value(KEY_SPLITTER_SIZES).toByteArray());
	ui->leftPanel->restorePanelGeometry(s.value(KEY_LPANEL_GEOMETRY).toByteArray());
	ui->leftPanel->restorePanelState(s.value(KEY_LPANEL_STATE).toByteArray());
	ui->rightPanel->restorePanelGeometry(s.value(KEY_RPANEL_GEOMETRY).toByteArray());
	ui->rightPanel->restorePanelState(s.value(KEY_RPANEL_STATE).toByteArray());

	ui->commandLine->addItems(s.value(KEY_LAST_COMMANDS_EXECUTED).toStringList());
	ui->commandLine->lineEdit()->clear();

	show();

	Panel lastActivePanel = (Panel)CSettings().value(KEY_LAST_ACTIVE_PANEL, LeftPanel).toInt();
	if (lastActivePanel == LeftPanel)
		ui->leftPanel->setFocusToFileList();
	else
		ui->rightPanel->setFocusToFileList();
}

void CMainWindow::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close)
	{
		CSettings settings;
		settings.setValue(KEY_GEOMETRY, saveGeometry());
		settings.setValue(KEY_STATE, saveState());
		settings.setValue(KEY_SPLITTER_SIZES, ui->splitter->saveState());
		settings.setValue(KEY_LPANEL_GEOMETRY, ui->leftPanel->savePanelGeometry());
		settings.setValue(KEY_RPANEL_GEOMETRY, ui->rightPanel->savePanelGeometry());
		settings.setValue(KEY_LPANEL_STATE, ui->leftPanel->savePanelState());
		settings.setValue(KEY_RPANEL_STATE, ui->rightPanel->savePanelState());

		emit closed(); // Is used to close all child windows
		emit fileQuickVewFinished(); // Cleaning up quick view widgets, if any
	}

	QMainWindow::closeEvent(e);
}

void CMainWindow::itemActivated(qulonglong hash, CPanelWidget *panel)
{
	if (!ui->commandLine->currentText().isEmpty())
		return;

	const FileOperationResultCode result = _controller->itemHashExists(panel->panelPosition(), hash) ? _controller->itemActivated(hash, panel->panelPosition()) : rcObjectDoesntExist;
	switch (result)
	{
	case rcObjectDoesntExist:
		QMessageBox(QMessageBox::Warning, "Error", "The file doesn't exist.").exec();
		break;
	case rcFail:
		QMessageBox(QMessageBox::Critical, "Error", QString("Failed to launch ")+_controller->itemByHash(panel->panelPosition(), hash).absoluteFilePath()).exec();
		break;
	case rcDirNotAccessible:
		QMessageBox(QMessageBox::Critical, "No access", "This item is not accessible.").exec();
		break;
	default:
		break;
	}
}

void CMainWindow::currentPanelChanged(QStackedWidget *panel)
{
	_currentPanel = panel;
	_currentFileList = dynamic_cast<CPanelWidget*>(panel->widget(0));
	if (panel)
	{
		_otherPanel = panel == ui->leftWidget ? ui->rightWidget : ui->leftWidget;
		_otherFileList = dynamic_cast<CPanelWidget*>(_otherPanel->widget(0));
		assert(_otherPanel && _otherFileList);
	}
	else
	{
		_otherPanel = 0;
		_otherFileList = 0;
	}

	if (_currentFileList)
	{
		_controller->activePanelChanged(_currentFileList->panelPosition());
		CSettings().setValue(KEY_LAST_ACTIVE_PANEL, _currentFileList->panelPosition());
		ui->fullPath->setText(_controller->panel(_currentFileList->panelPosition()).currentDirPath());
		CPluginEngine::get().currentPanelChanged(_currentFileList->panelPosition());
		_commandLineCompleter.setModel(_currentFileList->sortModel());
	}
	else
		_commandLineCompleter.setModel(0);
}

bool CMainWindow::widgetBelongsToHierarchy(QWidget * const widget, QObject * const hierarchy)
{
	if (widget == hierarchy)
		return true;

	const auto& children = hierarchy->children();
	if (children.contains(widget))
		return true;

	for (const auto& child: children)
		if (widgetBelongsToHierarchy(widget, child))
			return true;

	return false;
}

void CMainWindow::splitterContextMenuRequested(QPoint pos)
{
	const QPoint globalPos = dynamic_cast<QWidget*>(sender())->mapToGlobal(pos);
	QMenu menu;
	menu.addAction("50%");
	QAction * selectedItem = menu.exec(globalPos);
	if (selectedItem)
	{
		const int width = (ui->leftPanel->width() + ui->rightPanel->width()) / 2;
		QList<int> sizes;
		sizes.push_back(width);
		sizes.push_back(width);

		ui->splitter->setSizes(sizes);
	}
}

void CMainWindow::copySelectedFiles()
{
	if (_currentFileList && _otherFileList)
		copyFiles(_controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes()), _otherFileList->currentDir());
}

void CMainWindow::moveSelectedFiles()
{
	if (_currentFileList && _otherFileList)
		moveFiles(_controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes()), _otherFileList->currentDir());
}

void CMainWindow::deleteFiles()
{
	if (!_currentFileList)
		return;

#ifdef _WIN32
	auto items = _controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
	std::vector<std::wstring> paths;
	for (auto& item: items)
		paths.emplace_back(toNativeSeparators(item.absoluteFilePath()).toStdWString());
	CShell::deleteItems(paths, true, (void*)winId());
#else
	deleteFilesIrrevocably();
#endif
}

void CMainWindow::deleteFilesIrrevocably()
{
	if (!_currentFileList)
		return;

	auto items = _controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
#ifdef _WIN32
	std::vector<std::wstring> paths;
	for (auto& item: items)
		paths.emplace_back(toNativeSeparators(item.absoluteFilePath()).toStdWString());
	CShell::deleteItems(paths, false, (void*)winId());
#else
	if (QMessageBox::question(this, "Are you sure?", QString("Do you want to delete the selected files and folders completely?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		CDeleteProgressDialog * dialog = new CDeleteProgressDialog(items, _otherPanel->currentDir(), this);
		connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
		dialog->show();
	}
#endif
}

void CMainWindow::createFolder()
{
	if (!_currentFileList)
		return;

	const auto currentItem = _currentFileList->currentItemHash() != 0 ? _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()) : CFileSystemObject();
	const QString currentItemName = !currentItem.isCdUp() ? currentItem.fullName() : QString();
	const QString dirName = QInputDialog::getText(this, "New folder", "Enter the name for the new directory", QLineEdit::Normal, currentItemName);
	if (!dirName.isEmpty())
	{
		const bool ok = _controller->createFolder(_currentFileList->currentDir(), dirName);
		assert(ok);
	}
}

void CMainWindow::createFile()
{
	const QString fileName = QInputDialog::getText(this, "New file", "Enter the name for the new file");
	if (!fileName.isEmpty())
	{
		const bool ok = _controller->createFile(_currentFileList->currentDir(), fileName);
		assert(ok);
	}
}

// Other UI commands
void CMainWindow::viewFile()
{
	CPluginEngine::get().viewCurrentFile();
}

void CMainWindow::editFile()
{
	QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
	QString currentFile = _currentFileList ? _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).absoluteFilePath() : QString();
	if (!editorPath.isEmpty() && !currentFile.isEmpty())
	{
		const QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
		if (!editorPath.isEmpty() && !QProcess::startDetached(CSettings().value(KEY_EDITOR_PATH).toString(), QStringList() << currentFile))
			QMessageBox::information(this, "Error", QString("Cannot launch ")+editorPath);
	}
}

void CMainWindow::openTerminal()
{
	_controller->openTerminal(_currentFileList->currentDir());
}

void CMainWindow::showRecycleBInContextMenu(QPoint pos)
{
	pos = ui->btnDelete->mapToGlobal(pos);
	CShell::recycleBinContextMenu(pos.x(), pos.y(), (void*)winId());
}

#include <QLayout>

void CMainWindow::toggleQuickView()
{
	if (_quickViewActive)
	{
		_quickViewActive = false;
		assert(_currentPanel->count() == 2 || _otherPanel->count() == 2);
		if (_currentPanel->count() == 2)
			_currentPanel->removeWidget(_currentPanel->widget(1));
		else
			_otherPanel->removeWidget(_otherPanel->widget(1));

		emit fileQuickVewFinished();
	}
	else
		quickViewCurrentFile();
}

void CMainWindow::currentItemChanged(Panel /*p*/, qulonglong /*itemHash*/)
{
	if (_quickViewActive)
		quickViewCurrentFile();
}

bool CMainWindow::executeCommand(QString commandLineText)
{
	if (!_currentFileList || commandLineText.isEmpty())
		return false;

	CShell::executeShellCommand(commandLineText, _currentFileList->currentDir());
	CSettings().setValue(KEY_LAST_COMMANDS_EXECUTED, ui->commandLine->items());
	clearCommandLineAndRestoreFocus();

	return true;
}

void CMainWindow::selectPreviousCommandInTheCommandLine()
{
	ui->commandLine->selectPreviousItem();
	ui->commandLine->setFocus();
}

void CMainWindow::clearCommandLineAndRestoreFocus()
{
	ui->commandLine->reset();
	_currentFileList->setFocusToFileList();
}

void CMainWindow::pasteCurrentFileName()
{
	if (_currentFileList && _currentFileList->currentItemHash() != 0)
	{
		const QString textToAdd = _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).fullName();
		const QString newText = ui->commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->commandLine->lineEdit()->text() + " " + textToAdd);
		ui->commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::pasteCurrentFilePath()
{
	if (_currentFileList && _currentFileList->currentItemHash() != 0)
	{
		const QString textToAdd = _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).absoluteFilePath();
		const QString newText = ui->commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->commandLine->lineEdit()->text() + " " + textToAdd);
		ui->commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::refresh()
{
	if (_currentFileList)
		_controller->refreshPanelContents(_currentFileList->panelPosition());
}

void CMainWindow::showHiddenFiles()
{
	CSettings().setValue(KEY_INTERFACE_SHOW_HIDDEN_FILES, ui->action_Show_hidden_files->isChecked());
	_controller->refreshPanelContents(LeftPanel);
	_controller->refreshPanelContents(RightPanel);
}

void CMainWindow::showAllFilesFromCurrentFolderAndBelow()
{
	if (_currentFileList)
		_currentFileList->fillFromList(recurseDirectoryItems(_currentFileList->currentDir(), false), false, nopOther);
}

void CMainWindow::openSettingsDialog()
{
	CSettingsDialog settings;
	settings.addSettingsPage(new CSettingsPageInterface);
	settings.addSettingsPage(new CSettingsPageOperations);
	settings.addSettingsPage(new CSettingsPageEdit);
	settings.addSettingsPage(new CSettingsPageOther);
	connect(&settings, SIGNAL(settingsChanged()), SLOT(settingsChanged()));
	settings.exec();
}

void CMainWindow::calculateOccupiedSpace()
{
	if (!_currentFileList)
		return;

	const FilesystemObjectsStatistics stats = _controller->calculateStatistics(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
	if (stats.empty())
		return;

	QMessageBox::information(this, "Occupied space", QString("Statistics for the selected items(including subitems):\nFiles: %1\nFolders: %2\nOccupied space: %3").
							 arg(stats.files).arg(stats.folders).arg(fileSizeToString(stats.occupiedSpace)));
}

void CMainWindow::settingsChanged()
{
	_controller->settingsChanged();
}

void CMainWindow::focusChanged(QWidget * /*old*/, QWidget * now)
{
	if (!now)
		return;

	for (int i = 0; i < ui->leftWidget->count(); ++i)
		if (now == ui->leftWidget || widgetBelongsToHierarchy(now, ui->leftWidget->widget(i)))
			currentPanelChanged(ui->leftWidget);

	for (int i = 0; i < ui->rightWidget->count(); ++i)
		if (now == ui->rightWidget || widgetBelongsToHierarchy(now, ui->rightWidget->widget(i)))
			currentPanelChanged(ui->rightWidget);
}

void CMainWindow::createToolMenuEntries(std::vector<CPluginProxy::MenuTree> menuEntries)
{
	QMenuBar * menu = menuBar();
	if (!menu)
		return;

	static QMenu * toolMenu = 0; // Shouldn't have to be static, but 2 subsequent calls to this method result in "Tools" being added twice. QMenuBar needs event loop to update its children?..
	auto topLevelMenus = menu->findChildren<QMenu*>();
	for(auto topLevelMenu: topLevelMenus)
	{
		if (topLevelMenu->title() == "Tools")
		{
			toolMenu = topLevelMenu;
			break;
		}
	}

	if (!toolMenu)
	{
		toolMenu = new QMenu("Tools");
		menu->addMenu(toolMenu);
	}

	for(const auto& menuTree: menuEntries)
	{
		addToolMenuEntriesRecursively(menuTree, toolMenu);
	}

	toolMenu->addSeparator();
}

void CMainWindow::addToolMenuEntriesRecursively(CPluginProxy::MenuTree entry, QMenu* toolMenu)
{
	assert(toolMenu);
	QAction* action = toolMenu->addAction(entry.name);
	QObject::connect(action, &QAction::triggered, [entry](bool){entry.handler();});
	for(const auto& childEntry: entry.children)
		addToolMenuEntriesRecursively(childEntry, toolMenu);
}

bool CMainWindow::fileListReturnPressed()
{
	if (_currentFileList)
		return executeCommand(ui->commandLine->currentText());
	return false;
}

void CMainWindow::quickViewCurrentFile()
{
	if (_quickViewActive)
	{
		assert(_otherPanel->count() == 2);
		_otherPanel->removeWidget(_otherPanel->widget(1));
		emit fileQuickVewFinished();
	}

	QMainWindow * viewerWindow = CPluginEngine::get().createViewerWindowForCurrentFile();
	if (!viewerWindow)
		return;

	connect(this, SIGNAL(fileQuickVewFinished()), viewerWindow, SLOT(deleteLater()));

	_otherPanel->setCurrentIndex(_otherPanel->addWidget(viewerWindow->centralWidget()));
	_quickViewActive = true;
}

#include "progressdialogs/ccopymovedialog.h"
#include "progressdialogs/cdeleteprogressdialog.h"
#include "cmainwindow.h"
#include "ui_cmainwindow.h"
#include "settings.h"
#include "settings/csettings.h"
#include "shell/cshell.h"
#include "settingsui/csettingsdialog.h"
#include "settings/csettingspageinterface.h"
#include "settings/csettingspageedit.h"
#include "settings/csettingspageother.h"
#include "pluginengine/cpluginengine.h"
#include "panel/filelistwidget/cfilelistview.h"

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

CMainWindow::CMainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::CMainWindow),
	_controller(std::make_shared<CController>()),
	_currentPanel(0),
	_otherPanel(0)
{
	ui->setupUi(this);

	_controller->pluginProxy().setToolMenuEntryCreatorImplementation(CPluginProxy::CreateToolMenuEntryImplementationType(std::bind(&CMainWindow::createToolMenuEntries, this, std::placeholders::_1)));

	_currentPanel = ui->leftPanel;
	_otherPanel   = ui->rightPanel;

	connect(ui->leftPanel, SIGNAL(itemActivated(qulonglong,CPanelWidget*)), SLOT(itemActivated(qulonglong,CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(itemActivated(qulonglong,CPanelWidget*)), SLOT(itemActivated(qulonglong,CPanelWidget*)));

	connect(ui->leftPanel, SIGNAL(backSpacePressed(CPanelWidget*)), SLOT(backSpacePressed(CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(backSpacePressed(CPanelWidget*)), SLOT(backSpacePressed(CPanelWidget*)));

	connect(ui->leftPanel, SIGNAL(stepBackRequested(CPanelWidget*)), SLOT(stepBackRequested(CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(stepBackRequested(CPanelWidget*)), SLOT(stepBackRequested(CPanelWidget*)));

	connect(ui->leftPanel, SIGNAL(stepForwardRequested(CPanelWidget*)), SLOT(stepForwardRequested(CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(stepForwardRequested(CPanelWidget*)), SLOT(stepForwardRequested(CPanelWidget*)));

	connect(ui->leftPanel, SIGNAL(focusReceived(CPanelWidget*)), SLOT(currentPanelChanged(CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(focusReceived(CPanelWidget*)), SLOT(currentPanelChanged(CPanelWidget*)));

	connect(ui->leftPanel, SIGNAL(folderPathSet(QString,const CPanelWidget*)), SLOT(folderPathSet(QString,const CPanelWidget*)));
	connect(ui->rightPanel, SIGNAL(folderPathSet(QString,const CPanelWidget*)), SLOT(folderPathSet(QString,const CPanelWidget*)));

	connect(ui->rightPanel, SIGNAL(itemNameEdited(Panel,qulonglong,QString)), SLOT(itemNameEdited(Panel,qulonglong,QString)));
	connect(ui->leftPanel, SIGNAL(itemNameEdited(Panel,qulonglong,QString)), SLOT(itemNameEdited(Panel,qulonglong,QString)));

	connect(ui->leftPanel->fileListView(), SIGNAL(ctrlEnterPressed()), SLOT(pasteCurrentFileName()));
	connect(ui->rightPanel->fileListView(), SIGNAL(ctrlEnterPressed()), SLOT(pasteCurrentFileName()));
	connect(ui->leftPanel->fileListView(), SIGNAL(ctrlShiftEnterPressed()), SLOT(pasteCurrentFilePath()));
	connect(ui->rightPanel->fileListView(), SIGNAL(ctrlShiftEnterPressed()), SLOT(pasteCurrentFilePath()));

	ui->leftPanel->fileListView()->installEventFilter(this);
	ui->rightPanel->fileListView()->installEventFilter(this);

	initButtons();
	initActions();

	ui->leftPanel->setPanelPosition(LeftPanel);
	ui->rightPanel->setPanelPosition(RightPanel);

	ui->fullPath->clear();

	connect(ui->splitter, SIGNAL(customContextMenuRequested(QPoint)), SLOT(splitterContextMenuRequested(QPoint)));

	connect(ui->leftPanel->fileListView(), SIGNAL(returnPressed()), SLOT(executeCommand()));
	connect(ui->rightPanel->fileListView(), SIGNAL(returnPressed()), SLOT(executeCommand()));
	connect(ui->commandLine->lineEdit(), SIGNAL(returnPressed()), SLOT(executeCommand()));
	dynamic_cast<CHistoryComboBox*>(ui->commandLine)->setHistoryMode(true);
}

void CMainWindow::initButtons()
{
	connect(ui->btnView, SIGNAL(clicked()), SLOT(viewFile()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F3"), this, SLOT(viewFile()), 0, Qt::ApplicationShortcut)));

	connect(ui->btnEdit, SIGNAL(clicked()), SLOT(editFile()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F4"), this, SLOT(editFile()), 0, Qt::ApplicationShortcut)));

	connect(ui->btnCopy, SIGNAL(clicked()), SLOT(copyFiles()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F5"), this, SLOT(copyFiles()), 0, Qt::ApplicationShortcut)));

	connect(ui->btnMove, SIGNAL(clicked()), SLOT(moveFiles()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F6"), this, SLOT(moveFiles()), 0, Qt::ApplicationShortcut)));

	connect(ui->btnNewFolder, SIGNAL(clicked()), SLOT(createFolder()));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F7"), this, SLOT(createFolder()), 0, Qt::ApplicationShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+F7"), this, SLOT(createFile()), 0, Qt::ApplicationShortcut)));

	connect(ui->btnDelete, SIGNAL(clicked()), SLOT(deleteFiles()));
	connect(ui->btnDelete, SIGNAL(customContextMenuRequested(QPoint)), SLOT(showRecycleBInContextMenu(QPoint)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("F8"), this, SLOT(deleteFiles()), 0, Qt::ApplicationShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Delete"), this, SLOT(deleteFiles()), 0, Qt::ApplicationShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+F8"), this, SLOT(deleteFilesIrrevocably()), 0, Qt::ApplicationShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Shift+Delete"), this, SLOT(deleteFilesIrrevocably()), 0, Qt::ApplicationShortcut)));

	// Command line
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Ctrl+E"), this, SLOT(cycleLastCommands()), 0, Qt::ApplicationShortcut)));
	_shortcuts.push_back(std::shared_ptr<QShortcut>(new QShortcut(QKeySequence("Esc"), this, SLOT(clearCommandLineAndRestoreFocus()), 0, Qt::WidgetWithChildrenShortcut)));
}

void CMainWindow::initActions()
{
	connect(ui->actionOpen_Console_Here, SIGNAL(triggered()), SLOT(openTerminal()));
	connect(ui->actionExit, SIGNAL(triggered()), qApp, SLOT(quit()));

	ui->action_Show_hidden_files->setChecked(CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool());
	connect(ui->action_Show_hidden_files, SIGNAL(triggered()), SLOT(showHiddenFiles()));
	connect(ui->actionShowAllFiles, SIGNAL(triggered()), SLOT(showAllFilesFromCurrentFolderAndBelow()));
	connect(ui->action_Settings, SIGNAL(triggered()), SLOT(openSettingsDialog()));
}

// For manual focus management
void CMainWindow::tabKeyPressed()
{
	if (_currentPanel == ui->leftPanel)
	{
		_currentPanel = ui->rightPanel;
		_otherPanel = ui->leftPanel;
	}
	else
	{
		_currentPanel = ui->leftPanel;
		_otherPanel = ui->rightPanel;
	}
	_currentPanel->setFocusToFileList();
}


CMainWindow::~CMainWindow()
{
	delete ui;
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
	}

	QMainWindow::closeEvent(e);
}

bool CMainWindow::eventFilter(QObject * watched, QEvent * event)
{
	if (watched == (QObject*)ui->leftPanel->fileListView() || watched == (QObject*)ui->rightPanel->fileListView())
	{
		if (event && event->type() == QEvent::KeyPress)
		{
			QKeyEvent * keyEvent = dynamic_cast<QKeyEvent*>(event);
			if (keyEvent && keyEvent->key() != Qt::Key_Space && keyEvent->key() != Qt::Key_Enter && keyEvent->key() != Qt::Key_Return && !keyEvent->text().isEmpty())
			{
				ui->commandLine->setFocus();
				ui->commandLine->event(event);
				return false;
			}
		}
	}

	return false;
}

void CMainWindow::itemActivated(qulonglong hash, CPanelWidget *panel)
{
	if (!ui->commandLine->currentText().isEmpty())
		return;

	const FileOperationResultCode result = _controller->itemActivated(hash, panel->panelPosition());
	switch (result)
	{
	case rcObjectDoesntExist:
		QMessageBox(QMessageBox::Warning, "Error", "The file doesn't exist.").exec();
		break;
	case rcFail:
		QMessageBox(QMessageBox::Critical, "Error", "Failed to launch file.").exec();
		break;
	case rcDirNotAccessible:
		QMessageBox(QMessageBox::Critical, "No access", "This item is not accessible.").exec();
		break;
	default:
		break;
	}
}

void CMainWindow::backSpacePressed(CPanelWidget * widget)
{
	_controller->navigateUp(widget->panelPosition());
}

void CMainWindow::stepBackRequested(CPanelWidget *panel)
{
	_controller->navigateBack(panel->panelPosition());
}

void CMainWindow::stepForwardRequested(CPanelWidget *panel)
{
	_controller->navigateForward(panel->panelPosition());
}

void CMainWindow::currentPanelChanged(CPanelWidget *panel)
{
	_currentPanel = panel;
	_otherPanel   = panel == ui->leftPanel ? ui->rightPanel : ui->leftPanel;

	if (panel)
	{
		_controller->activePanelChanged(panel->panelPosition());
		CSettings().setValue(KEY_LAST_ACTIVE_PANEL, panel->panelPosition());
		ui->fullPath->setText(_controller->panel(panel->panelPosition()).currentDirPath());
		CPluginEngine::get().currentPanelChanged(panel->panelPosition());
	}
}

void CMainWindow::folderPathSet(QString path, const CPanelWidget *panel)
{
	_controller->setPath(panel->panelPosition(), path);
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

void CMainWindow::copyFiles()
{
	if (!_currentPanel || !_otherPanel)
		return;

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationCopy, _controller->items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes()), _otherPanel->currentDir(), this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->show();
}

void CMainWindow::moveFiles()
{
	if (!_currentPanel || !_otherPanel)
		return;

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationMove, _controller->items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes()), _otherPanel->currentDir(), this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->show();
}

void CMainWindow::deleteFiles()
{
	if (!_currentPanel)
		return;

#ifdef _WIN32
	auto items = _controller->items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes());
	std::vector<std::wstring> paths;
	for (auto& item: items)
		paths.emplace_back(item.absoluteFilePath().toStdWString());
	CShell::deleteItems(paths, true, (void*)winId());
#else
	deleteFilesIrrevocably();
#endif
}

void CMainWindow::deleteFilesIrrevocably()
{
	if (!_currentPanel)
		return;

	auto items = _controller->items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes());
#ifdef _WIN32
	std::vector<std::wstring> paths;
	for (auto& item: items)
		paths.emplace_back(item.absoluteFilePath().toStdWString());
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
	const QString dirName = QInputDialog::getText(this, "New folder", "Enter the name for the new directory");
	if (!dirName.isEmpty())
	{
		const bool ok = _controller->createFolder(_currentPanel->currentDir(), dirName);
		assert(ok);
	}
}

void CMainWindow::createFile()
{
	const QString fileName = QInputDialog::getText(this, "New file", "Enter the name for the new file");
	if (!fileName.isEmpty())
	{
		const bool ok = _controller->createFile(_currentPanel->currentDir(), fileName);
		assert(ok);
	}
}

void CMainWindow::itemNameEdited(Panel panel, qulonglong hash, QString newName)
{
	CFileSystemObject& item = _controller->itemByHash(panel, hash);
	if (item.rename(newName, true) != rcOk)
		QMessageBox::warning(this, "Failed to rename a file", QString("Failed to rename a file ") + item.fileName() + " to " + newName);
}


// Other UI commands
void CMainWindow::viewFile()
{
	CPluginEngine::get().viewCurrentFile();
}

void CMainWindow::editFile()
{
	QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
	QString currentFile = _currentPanel ? _controller->itemByHash(_currentPanel->panelPosition(), _currentPanel->currentItemHash()).absoluteFilePath() : QString();
	if (!editorPath.isEmpty() && !currentFile.isEmpty())
	{
		const QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
		if (!editorPath.isEmpty() && !QProcess::startDetached(CSettings().value(KEY_EDITOR_PATH).toString(), QStringList() << currentFile))
			QMessageBox::information(this, "Error", QString("Cannot launch ")+editorPath);
	}
}

void CMainWindow::openTerminal()
{
	_controller->openTerminal(_currentPanel->currentDir());
}

void CMainWindow::showRecycleBInContextMenu(QPoint pos)
{
	pos = ui->btnDelete->mapToGlobal(pos);
	CShell::recycleBinContextMenu(pos.x(), pos.y(), (void*)winId());
}

void CMainWindow::executeCommand()
{
	if (!_currentPanel)
		return;

	const QString commandLine = ui->commandLine->lineEdit()->text();
	if (commandLine.isEmpty())
		return;

	CShell::executeShellCommand(commandLine, _currentPanel->currentDir());

	QStringList commands;
	for (int i = 0; i < ui->commandLine->count(); ++i)
		commands.push_back(ui->commandLine->itemText(i));
	CSettings().setValue(KEY_LAST_COMMANDS_EXECUTED, commands);
	ui->commandLine->clearEditText();
}

void CMainWindow::cycleLastCommands()
{
	ui->commandLine->cycleLastCommands();
	ui->commandLine->setFocus();
}

void CMainWindow::clearCommandLineAndRestoreFocus()
{
	ui->commandLine->reset();
	_currentPanel->setFocusToFileList();
}

void CMainWindow::pasteCurrentFileName()
{
	if (_currentPanel && _currentPanel->currentItemHash() != 0)
	{
		const QString textToAdd = _controller->itemByHash(_currentPanel->panelPosition(), _currentPanel->currentItemHash()).fileName();
		const QString newText = ui->commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->commandLine->lineEdit()->text() + " " + textToAdd);
		ui->commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::pasteCurrentFilePath()
{
	if (_currentPanel && _currentPanel->currentItemHash() != 0)
	{
		const QString textToAdd = _controller->itemByHash(_currentPanel->panelPosition(), _currentPanel->currentItemHash()).absoluteFilePath();
		const QString newText = ui->commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->commandLine->lineEdit()->text() + " " + textToAdd);
		ui->commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::showHiddenFiles()
{
	CSettings().setValue(KEY_INTERFACE_SHOW_HIDDEN_FILES, ui->action_Show_hidden_files->isChecked());
	_controller->refreshPanelContents(LeftPanel);
	_controller->refreshPanelContents(RightPanel);
}

void CMainWindow::showAllFilesFromCurrentFolderAndBelow()
{
	if (_currentPanel)
		_currentPanel->fillFromList(recurseDirectoryItems(_currentPanel->currentDir(), false), false);
}

void CMainWindow::openSettingsDialog()
{
	CSettingsDialog settings;
	settings.addSettingsPage(new CSettingsPageInterface);
	settings.addSettingsPage(new CSettingsPageEdit);
	settings.addSettingsPage(new CSettingsPageOther);
	connect(&settings, SIGNAL(settingsChanged()), SLOT(settingsChanged()));
	settings.exec();
}

void CMainWindow::settingsChanged()
{
	_controller->settingsChanged();
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

#include "progressdialogs/ccopymovedialog.h"
#include "progressdialogs/cdeleteprogressdialog.h"
#include "cmainwindow.h"
#include "ui_cmainwindow.h"
#include "settings.h"
#include "settings/csettings.h"
#include "shell/cshell.h"
#include "settings/csettingsdialog.h"
#include "settings/csettingspageinterface.h"
#include "settings/csettingspageedit.h"

#include <assert.h>

#ifdef _WIN32
#include <Windows.h>
#endif

// Main window settings keys
#define KEY_RPANEL_STATE    "Ui/RPanel/State"
#define KEY_LPANEL_STATE    "Ui/LPanel/State"
#define KEY_RPANEL_GEOMETRY "Ui/RPanel/Geometry"
#define KEY_LPANEL_GEOMETRY "Ui/LPanel/Geometry"
#define KEY_GEOMETRY        "Ui/Geometry"
#define KEY_STATE           "Ui/State"
#define KEY_SPLITTER_SIZES  "Ui/Splitter"

CMainWindow::CMainWindow(QWidget *parent) :
	QMainWindow(parent),
	ui(new Ui::CMainWindow),
	_controller(CController::get()),
	_currentPanel(0),
	_otherPanel(0)
{
	ui->setupUi(this);

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

	initButtons();
	initActions();

	ui->leftPanel->setPanelPosition(LeftPanel);
	ui->rightPanel->setPanelPosition(RightPanel);

	ui->fullPath->clear();

	connect(ui->splitter, SIGNAL(customContextMenuRequested(QPoint)), SLOT(splitterContextMenuRequested(QPoint)));
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
}

void CMainWindow::initActions()
{
	connect(ui->actionOpen_Console_Here, SIGNAL(triggered()), SLOT(openTerminal()));
	connect(ui->actionExit, SIGNAL(triggered()), qApp, SLOT(quit()));
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
	show();
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
}

void CMainWindow::itemActivated(qulonglong hash, CPanelWidget *panel)
{
	const FileOperationResultCode result = _controller.itemActivated(hash, panel->panelPosition());
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
	_controller.navigateUp(widget->panelPosition());
}

void CMainWindow::stepBackRequested(CPanelWidget *panel)
{
	_controller.navigateBack(panel->panelPosition());
}

void CMainWindow::stepForwardRequested(CPanelWidget *panel)
{
	_controller.navigateForward(panel->panelPosition());
}

void CMainWindow::currentPanelChanged(CPanelWidget *panel)
{
	ui->fullPath->setText(_controller.panel(panel->panelPosition()).currentDirPath());

	_currentPanel = panel;
	_otherPanel   = panel == ui->leftPanel ? ui->rightPanel : ui->leftPanel;

	if (panel)
		_controller.pluginEngine().currentPanelChanged(panel->panelPosition());
}

void CMainWindow::folderPathSet(QString path, const CPanelWidget *panel)
{
	_controller.setPath(panel->panelPosition(), path);
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

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationCopy, _controller.items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes()), _otherPanel->currentDir(), this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->show();
}

void CMainWindow::moveFiles()
{
	if (!_currentPanel || !_otherPanel)
		return;

	CCopyMoveDialog * dialog = new CCopyMoveDialog(operationMove, _controller.items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes()), _otherPanel->currentDir(), this);
	connect(this, SIGNAL(closed()), dialog, SLOT(deleteLater()));
	dialog->show();
}

void CMainWindow::deleteFiles()
{
	if (!_currentPanel)
		return;

#ifdef _WIN32
	auto items = _controller.items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes());
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

	auto items = _controller.items(_currentPanel->panelPosition(), _currentPanel->selectedItemsHashes());
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
		const bool ok = _controller.createFolder(_currentPanel->currentDir(), dirName);
		assert(ok);
	}
}

void CMainWindow::createFile()
{
	const QString fileName = QInputDialog::getText(this, "New file", "Enter the name for the new file");
	if (!fileName.isEmpty())
	{
		const bool ok = _controller.createFile(_currentPanel->currentDir(), fileName);
		assert(ok);
	}
}


// Other UI commands
void CMainWindow::viewFile()
{
	_controller.pluginEngine().viewCurrentFile();
}

void CMainWindow::editFile()
{
	QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
	QString currentFile = _currentPanel ? _controller.itemByHash(_currentPanel->panelPosition(), _currentPanel->currentItemHash()).absoluteFilePath() : QString();
	if (!editorPath.isEmpty() && !currentFile.isEmpty())
	{
		const QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
		if (!editorPath.isEmpty() && !QProcess::startDetached(CSettings().value(KEY_EDITOR_PATH).toString(), QStringList() << currentFile))
			QMessageBox::information(this, "Error", QString("Cannot launch ")+editorPath);
	}
}

void CMainWindow::openTerminal()
{
	_controller.openTerminal(_currentPanel->currentDir());
}

void CMainWindow::showRecycleBInContextMenu(QPoint pos)
{
	pos = ui->btnDelete->mapToGlobal(pos);
	CShell::recycleBinContextMenu(pos.x(), pos.y(), (void*)winId());
}

void CMainWindow::showAllFilesFromCurrentFolderAndBelow()
{
	if (_currentPanel)
		_currentPanel->fillFromList(recurseDirectoryItems(_currentPanel->currentDir(), false));
}

void CMainWindow::openSettingsDialog()
{
	CSettingsDialog settings;
	settings.addSettingsPage(new CSettingsPageInterface);
	settings.addSettingsPage(new CSettingsPageEdit);
	connect(&settings, SIGNAL(settingsChanged()), SLOT(settingsChanged()));
	settings.exec();
}

void CMainWindow::settingsChanged()
{
	_controller.settingsChanged();
}

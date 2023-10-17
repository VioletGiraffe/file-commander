#include "cmainwindow.h"
#include "progressdialogs/ccopymovedialog.h"
#include "progressdialogs/cdeleteprogressdialog.h"
#include "progressdialogs/cfileoperationconfirmationprompt.h"
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
#include "panel/cpanelwidget.h"
#include "filesystemhelperfunctions.h"
#include "filessearchdialog/cfilessearchwindow.h"
#include "updaterUI/cupdaterdialog.h"
#include "aboutdialog/caboutdialog.h"
#include "widgets/cpersistentwindow.h"
#include "widgets/widgetutils.h"
#include "filesystemhelpers/filesystemhelpers.hpp"
#include "version.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cmainwindow.h"

#include "qtcore_helpers/qdatetime_helpers.hpp"
#include "qtcore_helpers/qstring_helpers.hpp"

#include <QCloseEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QWidgetList>
RESTORE_COMPILER_WARNINGS

#ifdef _WIN32
#include <Windows.h>
#endif

#include <memory>

// Main window settings keys
#define KEY_RPANEL_STATE      QSL("Ui/RPanel/State")
#define KEY_LPANEL_STATE      QSL("Ui/LPanel/State")
#define KEY_RPANEL_GEOMETRY   QSL("Ui/RPanel/Geometry")
#define KEY_LPANEL_GEOMETRY   QSL("Ui/LPanel/Geometry")
#define KEY_SPLITTER_SIZES    QSL("Ui/Splitter")
#define KEY_LAST_ACTIVE_PANEL QSL("Ui/LastActivePanel")

CMainWindow * CMainWindow::_instance = nullptr;

CMainWindow::CMainWindow(QWidget *parent) noexcept :
	QMainWindow(parent),
	ui(new Ui::CMainWindow)
{
	assert_r(!_instance);
	_instance = this;
	ui->setupUi(this);

	_leftPanelDisplayController.setPanelStackedWidget(ui->leftWidget);
	_leftPanelDisplayController.setPanelWidget(ui->leftPanel);

	_rightPanelDisplayController.setPanelStackedWidget(ui->rightWidget);
	_rightPanelDisplayController.setPanelWidget(ui->rightPanel);

	installEventFilter(new CPersistenceEnabler(QSL("UI/MainWindow"), this));

	QSplitterHandle * handle = ui->splitter->handle(1);
	handle->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(handle, &QSplitterHandle::customContextMenuRequested, this, &CMainWindow::splitterContextMenuRequested);

	connect(ui->_commandLine, &CHistoryComboBox::itemActivated, this, &CMainWindow::executeCommand);

	_commandLineCompleter.setCaseSensitivity(Qt::CaseInsensitive);
	_commandLineCompleter.setCompletionMode(QCompleter::InlineCompletion);
	_commandLineCompleter.setCompletionColumn(NameColumn);
	ui->_commandLine->setCompleter(&_commandLineCompleter);
	ui->_commandLine->setClearEditorOnItemActivation(true);
	ui->_commandLine->installEventFilter(this);

	_uiThreadTimer = new QTimer{ this };
}

CMainWindow::~CMainWindow() noexcept
{
	_uiThreadTimer->disconnect();

	_instance = nullptr;
	delete ui;
}

CMainWindow *CMainWindow::get()
{
	return _instance;
}

bool CMainWindow::created() const
{
	return _controller != nullptr;
}

// One-time initialization
void CMainWindow::onCreate()
{
	assert_debug_only(!created());

	initCore();

	CSettings s;

	// Check for updates
	if (s.value(KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY, true).toBool() &&
		s.value(KEY_LAST_UPDATE_CHECK_TIMESTAMP, fromTime_t(1)).toDateTime().msecsTo(QDateTime::currentDateTime()) >= 1000LL * 3600LL * 24LL)
	{
		s.setValue(KEY_LAST_UPDATE_CHECK_TIMESTAMP, QDateTime::currentDateTime());
		auto* dlg = new CUpdaterDialog(this, REPO_NAME, VERSION_STRING, true);
		connect(dlg, &QDialog::rejected, dlg, &QDialog::deleteLater);
		connect(dlg, &QDialog::accepted, dlg, &QDialog::deleteLater);
	}

	//qApp->setStyleSheet(s.value(KEY_INTERFACE_STYLE_SHEET).toString());
}

void CMainWindow::updateInterface()
{
	CSettings s;
	ui->splitter->restoreState(s.value(KEY_SPLITTER_SIZES).toByteArray());
	ui->leftPanel->restorePanelGeometry(s.value(KEY_LPANEL_GEOMETRY).toByteArray());
	ui->leftPanel->restorePanelState(s.value(KEY_LPANEL_STATE).toByteArray());
	ui->rightPanel->restorePanelGeometry(s.value(KEY_RPANEL_GEOMETRY).toByteArray());
	ui->rightPanel->restorePanelState(s.value(KEY_RPANEL_STATE).toByteArray());

	ui->_commandLine->addItems(s.value(KEY_LAST_COMMANDS_EXECUTED).toStringList());
	ui->_commandLine->lineEdit()->clear();

	show();

	if ((windowState() & Qt::WindowFullScreen) != 0)
		ui->actionFull_screen_mode->setChecked(true);

	const Panel lastActivePanel = (Panel)s.value(KEY_LAST_ACTIVE_PANEL, LeftPanel).toInt();
	if (lastActivePanel == LeftPanel)
		ui->leftPanel->setFocusToFileList();
	else
		ui->rightPanel->setFocusToFileList();
}

void CMainWindow::initButtons()
{
	connect(ui->btnView, &QPushButton::clicked, this, &CMainWindow::viewFile);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F3")), this, SLOT(viewFile()), nullptr, Qt::WidgetWithChildrenShortcut));

	connect(ui->btnEdit, &QPushButton::clicked, this, &CMainWindow::editFile);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F4")), this, SLOT(editFile()), nullptr, Qt::WidgetWithChildrenShortcut));

	connect(ui->btnCopy, &QPushButton::clicked, this, &CMainWindow::copySelectedFiles);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F5")), this, SLOT(copySelectedFiles()), nullptr, Qt::WidgetWithChildrenShortcut));

	connect(ui->btnMove, &QPushButton::clicked, this, &CMainWindow::moveSelectedFiles);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F6")), this, SLOT(moveSelectedFiles()), nullptr, Qt::WidgetWithChildrenShortcut));

	connect(ui->btnNewFolder, &QPushButton::clicked, this, &CMainWindow::createFolder);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F7")), this, SLOT(createFolder()), nullptr, Qt::WidgetWithChildrenShortcut));
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("Shift+F7")), this, SLOT(createFile()), nullptr, Qt::WidgetWithChildrenShortcut));

	connect(ui->btnDelete, &QPushButton::clicked, this, &CMainWindow::deleteFiles);
	connect(ui->btnDelete, &QPushButton::customContextMenuRequested, this, &CMainWindow::showRecycleBInContextMenu);
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("F8")), this, SLOT(deleteFiles()), nullptr, Qt::WidgetWithChildrenShortcut));
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("Delete")), this, SLOT(deleteFiles()), nullptr, Qt::WidgetWithChildrenShortcut));
#ifdef __APPLE__
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(Qt::CTRL + Qt::Key_Backspace), this, SLOT(deleteFiles()), nullptr, Qt::WidgetWithChildrenShortcut));
#endif
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("Shift+F8")), this, SLOT(deleteFilesIrrevocably()), nullptr, Qt::WidgetWithChildrenShortcut));
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("Shift+Delete")), this, SLOT(deleteFilesIrrevocably()), nullptr, Qt::WidgetWithChildrenShortcut));

	// Command line
	ui->_commandLine->setSelectPreviousItemShortcut(QKeySequence(QSL("Ctrl+E")));
	_shortcuts.push_back(std::make_shared<QShortcut>(QKeySequence(QSL("Ctrl+E")), this, SLOT(selectPreviousCommandInTheCommandLine()), nullptr, Qt::WidgetWithChildrenShortcut));
}

void CMainWindow::initActions()
{
	connect(ui->actionRefresh, &QAction::triggered, this, &CMainWindow::refresh);
	connect(ui->actionFind, &QAction::triggered, this, &CMainWindow::findFiles);

	connect(ui->actionCopy_current_item_s_path_to_clipboard, &QAction::triggered, this, [this]() {
		_controller->copyCurrentItemPathToClipboard();
	});

	ui->actionExit->setShortcut(QKeySequence::Quit);
	connect(ui->actionExit, &QAction::triggered, qApp, &QApplication::closeAllWindows);

	connect(ui->actionOpen_Console_Here, &QAction::triggered, this, [this]() {
		_controller->openTerminal(_currentFileList->currentDirPathNative());
	});

	connect(ui->actionOpen_Admin_console_here, &QAction::triggered, this, [this]() {
		_controller->openTerminal(_currentFileList->currentDirPathNative(), true);
	});

	ui->action_Show_hidden_files->setChecked(CSettings().value(KEY_INTERFACE_SHOW_HIDDEN_FILES, true).toBool());
#ifndef _WIN32
	ui->action_Show_hidden_files->setShortcut(QKeySequence{ QSL("Alt+H") }); // This is a common shortcut for Linux file managers
#else
	ui->action_Show_hidden_files->setShortcut(QKeySequence{ QSL("Ctrl+Alt+H") }); // Cannot use Alt+H because it's the accelerator for the "Help" item in the main menu
#endif

	connect(ui->action_Show_hidden_files, &QAction::triggered, this, &CMainWindow::showHiddenFiles);
	connect(ui->actionShowAllFiles, &QAction::triggered, this, &CMainWindow::showAllFilesFromCurrentFolderAndBelow);
	connect(ui->action_Settings, &QAction::triggered, this, &CMainWindow::openSettingsDialog);
	connect(ui->actionCalculate_occupied_space, &QAction::triggered, this, &CMainWindow::calculateOccupiedSpace);
	connect(ui->actionCalculate_each_folder_s_size, &QAction::triggered, this, &CMainWindow::calculateEachFolderSize);
	connect(ui->actionQuick_view, &QAction::triggered, this, &CMainWindow::toggleQuickView);
	connect(ui->actionFilter_items, &QAction::triggered, this, &CMainWindow::filterItemsByName);

	connect(ui->action_Invert_selection, &QAction::triggered, this, &CMainWindow::invertSelection);

	connect(ui->actionFull_screen_mode, &QAction::toggled, this, &CMainWindow::toggleFullScreenMode);
	connect(ui->actionTablet_mode, &QAction::toggled, this, &CMainWindow::toggleTabletMode);

	connect(ui->action_Check_for_updates, &QAction::triggered, this, &CMainWindow::checkForUpdates);
	connect(ui->actionAbout, &QAction::triggered, this, &CMainWindow::about);
}

// For manual focus management
void CMainWindow::tabKeyPressed()
{
	_otherFileList->setFocusToFileList();
}

bool CMainWindow::copyFiles(std::vector<CFileSystemObject>&& files, const QString & destDir)
{
	if (files.empty() || destDir.isEmpty())
		return false;

	// Fix for #91
	raise();
	activateWindow();

	const QString destPath = files.size() == 1 && files.front().isFile() ? cleanPath(destDir % nativeSeparator() % files.front().fullName()) : destDir;
	CFileOperationConfirmationPrompt prompt(tr("Copy files"), tr("Copy %1 %2 to").arg(files.size()).arg(files.size() > 1 ? QSL("files") : QSL("file")), toNativeSeparators(destPath), this);
	if (CSettings().value(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, true).toBool())
	{
		if (prompt.exec() != QDialog::Accepted)
			return false;
	}

	CCopyMoveDialog * dialog = new CCopyMoveDialog(this, operationCopy, std::move(files), toPosixSeparators(prompt.text()), this);
	connect(this, &CMainWindow::closed, dialog, &CCopyMoveDialog::deleteLater);
	dialog->show();

	return true;
}

bool CMainWindow::moveFiles(std::vector<CFileSystemObject>&& files, const QString & destDir)
{
	if (files.empty() || destDir.isEmpty())
		return false;

	// Fix for #91
	raise();
	activateWindow();

	CFileOperationConfirmationPrompt prompt(tr("Move files"), tr("Move %1 %2 to").arg(files.size()).arg(files.size() > 1 ? QSL("files") : QSL("file")), toNativeSeparators(destDir), this);
	if (CSettings().value(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, true).toBool())
	{
		if (prompt.exec() != QDialog::Accepted)
			return false;
	}

	CCopyMoveDialog * dialog = new CCopyMoveDialog(this, operationMove, std::move(files), toPosixSeparators(prompt.text()), this);
	connect(this, &CMainWindow::closed, dialog, &CCopyMoveDialog::deleteLater);
	dialog->show();

	return true;
}

void CMainWindow::closeEvent(QCloseEvent *e)
{
	if (e->type() == QCloseEvent::Close)
	{
		CSettings s;
		s.setValue(KEY_SPLITTER_SIZES, ui->splitter->saveState());
		s.setValue(KEY_LPANEL_GEOMETRY, ui->leftPanel->savePanelGeometry());
		s.setValue(KEY_RPANEL_GEOMETRY, ui->rightPanel->savePanelGeometry());
		s.setValue(KEY_LPANEL_STATE, ui->leftPanel->savePanelState());
		s.setValue(KEY_RPANEL_STATE, ui->rightPanel->savePanelState());

		emit closed(); // Is used to close all child windows
	}

	QMainWindow::closeEvent(e);
}

bool CMainWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == ui->_commandLine && event->type() == QEvent::KeyPress)
	{
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		if (keyEvent->key() == Qt::Key_Escape)
			clearCommandLineAndRestoreFocus();
	}

	return QMainWindow::eventFilter(watched, event);
}

void CMainWindow::itemActivated(qulonglong hash, CPanelWidget *panel)
{
	const auto result = _controller->itemHashExists(panel->panelPosition(), hash) ? _controller->itemActivated(hash, panel->panelPosition()) : FileOperationResultCode::ObjectDoesntExist;
	switch (result)
	{
	case FileOperationResultCode::ObjectDoesntExist:
		QMessageBox(QMessageBox::Warning, tr("Error"), tr("The file doesn't exist.")).exec();
		break;
	case FileOperationResultCode::Fail:
		QMessageBox(QMessageBox::Critical, tr("Error"), tr("Failed to launch %1").arg(_controller->itemByHash(panel->panelPosition(), hash).fullAbsolutePath())).exec();
		break;
	case FileOperationResultCode::DirNotAccessible:
		QMessageBox(QMessageBox::Critical, tr("No access"), tr("This item is not accessible.")).exec();
		break;
	default:
		break;
	}
}

void CMainWindow::currentPanelChanged(const Panel panel)
{
	if (panel == RightPanel)
	{
		_currentFileList = _rightPanelDisplayController.panelWidget();
		_otherFileList = _leftPanelDisplayController.panelWidget();
	}
	else if (panel == LeftPanel)
	{
		_currentFileList = _leftPanelDisplayController.panelWidget();
		_otherFileList = _rightPanelDisplayController.panelWidget();
	}
	else
		assert_unconditional_r("Invalid \'panel\' argument");

	if (!_currentFileList)
	{
		_commandLineCompleter.setModel(nullptr);
		return;
	}

	_controller->activePanelChanged(_currentFileList->panelPosition());
	CSettings().setValue(KEY_LAST_ACTIVE_PANEL, _currentFileList->panelPosition());
	ui->fullPath->setText(_controller->panel(_currentFileList->panelPosition()).currentDirPathNative());
	CPluginEngine::get().currentPanelChanged(_currentFileList->panelPosition());
	_commandLineCompleter.setModel(_currentFileList->sortModel());
}

void CMainWindow::uiThreadTimerTick()
{
	if (_controller)
		_controller->uiThreadTimerTick();
}

// Window title management (#143)
void CMainWindow::updateWindowTitleWithCurrentFolderNames()
{
	QString leftPanelDirName = _controller->panel(LeftPanel).currentDirName();
	if (leftPanelDirName.length() > 1 && leftPanelDirName.endsWith('/'))
		leftPanelDirName.chop(1);

	QString rightPanelDirName = _controller->panel(RightPanel).currentDirName();
	if (rightPanelDirName.length() > 1 && rightPanelDirName.endsWith('/'))
		rightPanelDirName.chop(1);

	setWindowTitle('[' % leftPanelDirName % "] / [" % rightPanelDirName % ']');
}

void CMainWindow::splitterContextMenuRequested(QPoint pos)
{
	const QPoint globalPos = dynamic_cast<QWidget*>(sender())->mapToGlobal(pos);
	QMenu menu;
	menu.addAction(QSL("50%"));
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
		// Some algorithms rely on trailing slash for distinguishing between files and folders for non-existent items
		copyFiles(_controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes()), _otherFileList->currentDirPathNative());
}

void CMainWindow::moveSelectedFiles()
{
	if (_currentFileList && _otherFileList)
		// Some algorithms rely on trailing slash for distinguishing between files and folders for non-existent items
		moveFiles(_controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes()), _otherFileList->currentDirPathNative());
}

void CMainWindow::deleteFiles()
{
	if (!_currentFileList)
		return;

#if defined _WIN32 || defined __APPLE__
	auto items = _controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
	std::vector<std::wstring> paths;
	paths.reserve(items.size());
	for (auto& item : items)
		paths.emplace_back(toNativeSeparators(item.fullAbsolutePath()).toStdWString());

	if (paths.empty())
		return;

#ifdef _WIN32
	auto* windowHandle = reinterpret_cast<void*>(winId());
#else
	void* windowHandle = nullptr;
#endif

	_controller->execOnWorkerThread([=, this]() {
		if (!OsShell::deleteItems(paths, true, windowHandle))
			_controller->execOnUiThread([this]() {
			QMessageBox::warning(this, tr("Error deleting items"), tr("Failed to delete the selected items"));
		});
	});

#else
	deleteFilesIrrevocably();
#endif
}

void CMainWindow::deleteFilesIrrevocably()
{
	if (!_currentFileList)
		return;

	auto items = _controller->items(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
	if (items.empty())
		return;
#ifdef _WIN32
	std::vector<std::wstring> paths;
	paths.reserve(items.size());
	for (auto& item : items)
		paths.emplace_back(toNativeSeparators(item.fullAbsolutePath()).toStdWString());

	_controller->execOnWorkerThread([=, this]() {
		if (!OsShell::deleteItems(paths, false, reinterpret_cast<void*>(winId())))
			_controller->execOnUiThread([this]() {
			QMessageBox::warning(this, tr("Error deleting items"), tr("Failed to delete the selected items"));
		});
	});
#else
	if (QMessageBox::question(this, tr("Are you sure?"), tr("Do you want to delete the selected files and folders completely?"), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
	{
		CDeleteProgressDialog * dialog = new CDeleteProgressDialog(this, std::move(items), _otherFileList->currentDirPathNative(), this);
		connect(this, &CMainWindow::closed, dialog, &CDeleteProgressDialog::deleteLater);
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

	QInputDialog dialog(this);
	dialog.setWindowIcon(QFileIconProvider().icon(QFileIconProvider::Folder));
	dialog.setWindowTitle(tr("New folder"));
	dialog.setLabelText(tr("Enter the name for the new directory"));
	dialog.setTextValue(currentItemName);

	const QString dirName = dialog.exec() == QDialog::Accepted ? dialog.textValue() : QString();// QInputDialog::getText(this, tr("New folder"), tr("Enter the name for the new directory"), QLineEdit::Normal, currentItemName);
	if (dirName.isEmpty())
		return;

	const auto result = _controller->createFolder(_currentFileList->currentDirPathNative(), toPosixSeparators(dirName));
	if (result == FileOperationResultCode::TargetAlreadyExists)
		QMessageBox::warning(this, tr("Item already exists"), tr("The folder %1 already exists.").arg(dirName));
	else if (result != FileOperationResultCode::Ok)
		QMessageBox::warning(this, tr("Failed to create item"), tr("Failed to create the folder %1").arg(dirName));
}

void CMainWindow::createFile()
{
	if (!_currentFileList)
		return;

	const auto currentItem = _currentFileList->currentItemHash() != 0 ? _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()) : CFileSystemObject();
	const QString currentItemName = !currentItem.isCdUp() ? currentItem.fullName() : QString();

	QInputDialog dialog(this);
	dialog.setWindowIcon(QFileIconProvider().icon(QFileIconProvider::File));
	dialog.setWindowTitle(tr("New file"));
	dialog.setLabelText(tr("Enter the name for the new file"));
	dialog.setTextValue(currentItemName);

	const QString fileName = dialog.exec() == QDialog::Accepted ? dialog.textValue() : QString();
	if (fileName.isEmpty())
		return;

	const auto result = _controller->createFile(_currentFileList->currentDirPathNative(), fileName);
	if (result == FileOperationResultCode::TargetAlreadyExists)
		QMessageBox::warning(this, tr("Item already exists"), tr("The file %1 already exists.").arg(fileName));
	else if (result != FileOperationResultCode::Ok)
		QMessageBox::warning(this, tr("Failed to create item"), tr("Failed to create the file %1").arg(fileName));
}

void CMainWindow::invertSelection()
{
	if (_currentFileList)
		_currentFileList->invertSelection();
}

// Other UI commands
void CMainWindow::viewFile()
{
	CPluginEngine::get().viewCurrentFile();
}

void CMainWindow::editFile()
{
	QString editorPath = CSettings().value(KEY_EDITOR_PATH).toString();
	if (FileSystemHelpers::resolvePath(editorPath).isEmpty())
	{
		if (QMessageBox::question(this, tr("Editor not configured"), tr("No editor program has been configured (or the specified path doesn't exist). Do you want to specify the editor now?")) == QMessageBox::Yes)
		{
#ifdef _WIN32
			const QString mask(tr("Executable files (*.exe *.cmd *.bat)"));
#else
			const QString mask;
#endif
			editorPath = QFileDialog::getOpenFileName(this, tr("Browse for editor program"), QString(), mask);
			if (editorPath.isEmpty())
				return;

			CSettings().setValue(KEY_EDITOR_PATH, editorPath);
		}
		else
			return;
	}

	const QString currentFile = _currentFileList ? _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).fullAbsolutePath() : QString();
	if (!currentFile.isEmpty())
	{
#ifdef __APPLE__
		const bool started = std::system((QString("open -n \"") + CSettings().value(KEY_EDITOR_PATH).toString() + "\" --args \"" + currentFile + "\"").toUtf8().constData()) == 0;
#else
		const bool started = QProcess::startDetached(editorPath, {currentFile});
#endif

		if (!started)
			QMessageBox::information(this, tr("Error"), tr("Cannot launch %1").arg(editorPath));
	}
}

void CMainWindow::showRecycleBInContextMenu(QPoint pos)
{
	const QPoint globalPos = ui->btnDelete->mapToGlobal(pos) * ui->btnDelete->devicePixelRatioF(); // These coordinates ar egoing directly into the system API so need to account for scaling that Qt tries to abstract away.
	OsShell::recycleBinContextMenu(globalPos.x(), globalPos.y(), reinterpret_cast<void*>(winId()));
}

void CMainWindow::toggleQuickView()
{
	if (!otherPanelDisplayController().quickViewActive())
		quickViewCurrentFile();
	else
		otherPanelDisplayController().endQuickView();
}

void CMainWindow::filterItemsByName()
{
	if (auto* panel = currentPanelDisplayController().panelWidget(); panel)
		panel->showFilterEditor();
}

void CMainWindow::currentItemChanged(Panel /*p*/, qulonglong /*itemHash*/)
{
	if (otherPanelDisplayController().quickViewActive())
		quickViewCurrentFile();
}

void CMainWindow::toggleFullScreenMode(bool fullscreen)
{
	if (fullscreen)
	{
		_windowStateBeforeFullscreen = (windowState() & Qt::WindowMaximized) ? MaximizedWindow : NormalWindow;
		showFullScreen();
	}
	else
	{
		if (_windowStateBeforeFullscreen == MaximizedWindow)
			showMaximized();
		else
			showNormal();
	}
}

void CMainWindow::toggleTabletMode(bool tabletMode)
{
	static const int defaultFontSize = QApplication::font().pointSize();

	ui->actionFull_screen_mode->toggle();

	QFont f = QApplication::font();
	f.setPointSize(tabletMode ? 24 : defaultFontSize);
	QApplication::setFont(f);

	auto widgets = QApplication::allWidgets();
	for (auto* widget : widgets)
	{
		if (widget)
			widget->setFont(f);
	}
}

bool CMainWindow::executeCommand(const QString& commandLineText)
{
	if (!_currentFileList || commandLineText.isEmpty())
		return false;

	OsShell::executeShellCommand(commandLineText, _currentFileList->currentDirPathNative());
	QTimer::singleShot(0, this, [this]() { CSettings().setValue(KEY_LAST_COMMANDS_EXECUTED, ui->_commandLine->items()); }); // Saving the list AFTER the combobox actually accepts the newly added item
	clearCommandLineAndRestoreFocus();

	return true;
}

void CMainWindow::selectPreviousCommandInTheCommandLine()
{
	ui->_commandLine->selectPreviousItem();
	ui->_commandLine->setFocus();
}

void CMainWindow::clearCommandLineAndRestoreFocus()
{
	ui->_commandLine->resetToLastSelected(true);
	_currentFileList->setFocusToFileList();
}

void CMainWindow::pasteCurrentFileName()
{
	if (_currentFileList && _currentFileList->currentItemHash() != 0)
	{
		QString textToAdd = _controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).fullName();
		if (textToAdd.contains(' '))
			textToAdd = '\"' % textToAdd % '\"';

		const QString newText = ui->_commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->_commandLine->lineEdit()->text() % ' ' % textToAdd);
		ui->_commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::pasteCurrentFilePath()
{
	if (_currentFileList && _currentFileList->currentItemHash() != 0)
	{
		QString textToAdd = toNativeSeparators(_controller->itemByHash(_currentFileList->panelPosition(), _currentFileList->currentItemHash()).fullAbsolutePath());
		if (textToAdd.contains(' '))
			textToAdd = '\"' % textToAdd % '\"';

		const QString newText = ui->_commandLine->lineEdit()->text().isEmpty() ? textToAdd : (ui->_commandLine->lineEdit()->text() % ' ' % textToAdd);
		ui->_commandLine->lineEdit()->setText(newText);
	}
}

void CMainWindow::refresh()
{
	if (_currentFileList)
		_controller->refreshPanelContents(_currentFileList->panelPosition());
}

void CMainWindow::findFiles()
{
	if (!_currentFileList)
		return;

	auto selectedHashes = _currentFileList->selectedItemsHashes(true);
	std::vector<QString> selectedPaths;
	if (!selectedHashes.empty())
	{
		selectedPaths.reserve(selectedHashes.size());
		for (const auto hash : selectedHashes)
			selectedPaths.push_back(_controller->activePanel().itemByHash(hash).fullAbsolutePath());
	}
	else
		selectedPaths.push_back(_currentFileList->currentDirPathNative());


	auto* fileSearchUi = new CFilesSearchWindow(selectedPaths, this);
	connect(this, &CMainWindow::closed, fileSearchUi, &CFilesSearchWindow::close);
	fileSearchUi->show();
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
		_controller->showAllFilesFromCurrentFolderAndBelow(_currentFileList->panelPosition());
}

void CMainWindow::openSettingsDialog()
{
	CSettingsDialog settings;
	settings.addSettingsPage(new CSettingsPageInterface);
	settings.addSettingsPage(new CSettingsPageOperations);
	settings.addSettingsPage(new CSettingsPageEdit);
	settings.addSettingsPage(new CSettingsPageOther);
	connect(&settings, &CSettingsDialog::settingsChanged, this, &CMainWindow::settingsChanged);

	settings.adjustSize();

	settings.exec();
}

void CMainWindow::calculateOccupiedSpace()
{
	if (!_currentFileList)
		return;

	const FilesystemObjectsStatistics stats = _controller->calculateStatistics(_currentFileList->panelPosition(), _currentFileList->selectedItemsHashes());
	if (stats.empty())
		return;

	QMessageBox::information(this, tr("Occupied space"), tr("Statistics for the selected items(including subitems):\nFiles: %1\nFolders: %2\nOccupied space: %3\n%4").
		arg(stats.files).arg(stats.folders).arg(fileSizeToString(stats.occupiedSpace), fileSizeToString(stats.occupiedSpace, 'B', QString{ ' ' })));
}

void CMainWindow::calculateEachFolderSize()
{
	if (!_currentFileList)
		return;

	for (const auto& item : _controller->panel(_currentFileList->panelPosition()).list())
	{
		if (item.second.isDir())
			_controller->displayDirSize(_currentFileList->panelPosition(), item.first);
	}
}

void CMainWindow::checkForUpdates()
{
	CSettings().setValue(KEY_LAST_UPDATE_CHECK_TIMESTAMP, QDateTime::currentDateTime());
	CUpdaterDialog(this, REPO_NAME, VERSION_STRING).exec();
}

void CMainWindow::about()
{
	CAboutDialog(this).exec();
}

void CMainWindow::settingsChanged()
{
	_controller->settingsChanged();
	ui->leftPanel->onSettingsChanged();
	ui->rightPanel->onSettingsChanged();

	qApp->setStyleSheet(CSettings().value(KEY_INTERFACE_STYLE_SHEET).toString());
}

void CMainWindow::focusChanged(QWidget * /*old*/, QWidget * now)
{
	if (!now)
		return;

	for (int i = 0; i < ui->leftWidget->count(); ++i)
		if (now == ui->leftWidget || WidgetUtils::widgetBelongsToHierarchy(now, ui->leftWidget->widget(i)))
			currentPanelChanged(LeftPanel);

	for (int i = 0; i < ui->rightWidget->count(); ++i)
		if (now == ui->rightWidget || WidgetUtils::widgetBelongsToHierarchy(now, ui->rightWidget->widget(i)))
			currentPanelChanged(RightPanel);
}

void CMainWindow::panelContentsChanged(Panel p, FileListRefreshCause /*operation*/)
{
	if (_currentFileList && p == _currentFileList->panelPosition())
		ui->fullPath->setText(_controller->panel(p).currentDirPathNative());

	updateWindowTitleWithCurrentFolderNames();
}

void CMainWindow::itemDiscoveryInProgress(Panel /*p*/, qulonglong /*itemHash*/, size_t /*progress*/, const QString& /*currentDir*/)
{
}

void CMainWindow::initCore()
{
	_controller = std::make_unique<CController>();
	ui->leftPanel->init(_controller.get());
	ui->rightPanel->init(_controller.get());

	_controller->activePanelChanged((Panel)CSettings().value(KEY_LAST_ACTIVE_PANEL, LeftPanel).toInt());

	connect(qApp, &QApplication::focusChanged, this, &CMainWindow::focusChanged);

	_controller->pluginProxy().setToolMenuEntryCreatorImplementation([this](const std::vector<CPluginProxy::MenuTree>& menuEntries) {createToolMenuEntries(menuEntries); });
	// Need to load the plugins only after the menu creator has been set
	_controller->loadPlugins();

	_currentFileList = ui->leftPanel;
	_otherFileList = ui->rightPanel;

	connect(ui->leftPanel->fileListView(), &CFileListView::ctrlEnterPressed, this, &CMainWindow::pasteCurrentFileName);
	connect(ui->rightPanel->fileListView(), &CFileListView::ctrlEnterPressed, this, &CMainWindow::pasteCurrentFileName);
	connect(ui->leftPanel->fileListView(), &CFileListView::ctrlShiftEnterPressed, this, &CMainWindow::pasteCurrentFilePath);
	connect(ui->rightPanel->fileListView(), &CFileListView::ctrlShiftEnterPressed, this, &CMainWindow::pasteCurrentFilePath);

	connect(ui->leftPanel, &CPanelWidget::currentItemChangedSignal, this, &CMainWindow::currentItemChanged);
	connect(ui->rightPanel, &CPanelWidget::currentItemChangedSignal, this, &CMainWindow::currentItemChanged);

	connect(ui->leftPanel, &CPanelWidget::itemActivated, this, &CMainWindow::itemActivated);
	connect(ui->rightPanel, &CPanelWidget::itemActivated, this, &CMainWindow::itemActivated);

	ui->leftPanel->fileListView()->addEventObserver(this);
	ui->rightPanel->fileListView()->addEventObserver(this);

	initButtons();
	initActions();

	ui->leftPanel->setPanelPosition(LeftPanel);
	ui->rightPanel->setPanelPosition(RightPanel);

	ui->fullPath->clear();

	ui->leftWidget->setCurrentIndex(0); // PanelWidget
	ui->rightWidget->setCurrentIndex(0); // PanelWidget

	_controller->panel(LeftPanel).addPanelContentsChangedListener(this);
	_controller->panel(RightPanel).addPanelContentsChangedListener(this);

	connect(_uiThreadTimer, &QTimer::timeout, this, &CMainWindow::uiThreadTimerTick);
	_uiThreadTimer->start(10);
}

void CMainWindow::createToolMenuEntries(const std::vector<CPluginProxy::MenuTree>& menuEntries)
{
	QMenuBar * menu = menuBar();
	if (!menu)
		return;

	static QMenu * toolMenu = nullptr; // Shouldn't have to be static, but 2 subsequent calls to this method result in "Tools" being added twice. QMenuBar needs event loop to update its children?..
									   // TODO: make it a class member

	for (auto* topLevelMenu : menu->findChildren<QMenu*>())
	{
		// TODO: make sure this plays nicely with localization (#145)
		if (topLevelMenu->title().remove('&') == QL1("Tools"))
		{
			toolMenu = topLevelMenu;
			break;
		}
	}

	if (!toolMenu)
	{
		// TODO: make sure this plays nicely with localization (#145)
		toolMenu = new QMenu(QSL("Tools"));
		menu->addMenu(toolMenu);
	}
	else
		toolMenu->addSeparator();

	for (const auto& menuTree : menuEntries)
		addToolMenuEntriesRecursively(menuTree, toolMenu);

	toolMenu->addSeparator();
}

void CMainWindow::addToolMenuEntriesRecursively(const CPluginProxy::MenuTree& entry, QMenu* toolMenu)
{
	assert_r(toolMenu);
	QAction* action = toolMenu->addAction(entry.name);

	if (entry.children.empty())
	{
		const auto handler = entry.handler;
		QObject::connect(action, &QAction::triggered, [handler](bool) {handler(); });
	}
	else
	{
		for (const auto& childEntry : entry.children)
			addToolMenuEntriesRecursively(childEntry, toolMenu);
	}
}

CPanelDisplayController& CMainWindow::currentPanelDisplayController()
{
	const auto panel = _controller->activePanelPosition();
	if (panel == RightPanel)
		return _rightPanelDisplayController;
	else
	{
		assert_r(panel != UnknownPanel);
		return _leftPanelDisplayController;
	}
}

CPanelDisplayController& CMainWindow::otherPanelDisplayController()
{
	const auto panel = _controller->activePanelPosition();
	if (panel == RightPanel)
		return _leftPanelDisplayController;
	else
	{
		assert_r(panel != UnknownPanel);
		return _rightPanelDisplayController;
	}
}

bool CMainWindow::fileListReturnPressed()
{
	return _currentFileList ? executeCommand(ui->_commandLine->currentText()) : false;
}

void CMainWindow::quickViewCurrentFile()
{
	otherPanelDisplayController().startQuickView(CPluginEngine::get().createViewerWindowForCurrentFile());
}

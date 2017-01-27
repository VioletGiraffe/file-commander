#pragma once

#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "panel/cpanelwidget.h"
#include "panel/filelistwidget/cfilelistview.h"
#include "cpanel.h"

DISABLE_COMPILER_WARNINGS
#include <QCompleter>
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <vector>
#include <memory>

namespace Ui {
	class CMainWindow;
}

class CPanelWidget;
class QShortcut;
class QStackedWidget;

class CMainWindow : public QMainWindow,
		private FileListReturnPressedObserver,
		private PanelContentsChangedListener
{
	Q_OBJECT

public:
	explicit CMainWindow(QWidget *parent = 0);
	~CMainWindow();
	static CMainWindow* get();

	void updateInterface();

	void initButtons();
	void initActions();

	// For manual focus management
	void tabKeyPressed ();

	bool copyFiles(const std::vector<CFileSystemObject>& files, const QString& destDir);
	bool moveFiles(const std::vector<CFileSystemObject>& files, const QString& destDir);

signals:
	// Is used to close all child windows
	void closed();
	// Is used to delete
	void fileQuickVewFinished();

protected:
	void showEvent(QShowEvent * e) override;
	void closeEvent(QCloseEvent * e) override;

private slots: // UI slots
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void splitterContextMenuRequested(QPoint pos);

// File operations UI slots
	void copySelectedFiles();
	void moveSelectedFiles();
	void deleteFiles();
	void deleteFilesIrrevocably();
	void createFolder();
	void createFile();

// Selection slots
	void invertSelection();

// Other UI commands
	void viewFile();
	void editFile();
	void showRecycleBInContextMenu(QPoint pos);
	void toggleQuickView();
	void currentItemChanged(Panel p, qulonglong itemHash);

	void toggleFullScreenMode(bool fullscreen);
	void toggleTabletMode(bool tabletMode);

// Command line
	// true if command was executed
	bool executeCommand(QString commandLineText);
	void selectPreviousCommandInTheCommandLine();
	void clearCommandLineAndRestoreFocus();
	void pasteCurrentFileName();
	void pasteCurrentFilePath();

// Main menu
	void refresh();
	void findFiles();
	void showHiddenFiles();
	void showAllFilesFromCurrentFolderAndBelow();
	void openSettingsDialog();
	void calculateOccupiedSpace();
	void checkForUpdates();
	void about();

// Settings
	void settingsChanged();

// Focus management
	void focusChanged(QWidget * old, QWidget * now);

private:
	void panelContentsChanged(Panel p, FileListRefreshCause operation) override;
	void itemDiscoveryInProgress(Panel p, qulonglong itemHash, size_t progress, const QString& currentDir) override;

private:
	void createToolMenuEntries(const std::vector<CPluginProxy::MenuTree>& menuEntries);
	void addToolMenuEntriesRecursively(CPluginProxy::MenuTree entry, QMenu* toolMenu);

	// For command line handling
	bool fileListReturnPressed() override;

	// Quick view
	void quickViewCurrentFile();

	// Helper functions
	static bool widgetBelongsToHierarchy(QWidget * const widget, QObject * const hierarchy);

	// Other
	void currentPanelChanged(QStackedWidget * panel);

	// Timer
	void uiThreadTimerTick();

	// Window title management (#143)
	void updateWindowTitleWithCurrentFolderNames();

private:
	Ui::CMainWindow              * ui;
	static CMainWindow*            _instance;

	QTimer                         _uiThreadTimer;

	std::unique_ptr<CController>   _controller;
	CPanelWidget                 * _currentFileList = nullptr;
	CPanelWidget                 * _otherFileList = nullptr;
	QStackedWidget               * _currentPanelWidget = nullptr;
	QStackedWidget               * _otherPanelWidget = nullptr;

	std::vector<std::shared_ptr<QShortcut> > _shortcuts;

	QCompleter                     _commandLineCompleter;

	bool                           _quickViewActive = false;
};


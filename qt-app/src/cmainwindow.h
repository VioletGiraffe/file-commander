#ifndef CMAINWINDOW_H
#define CMAINWINDOW_H

#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "panel/cpanelwidget.h"
#include "panel/filelistwidget/cfilelistview.h"

#include "QtAppIncludes"

#include <vector>
#include <memory>

namespace Ui {
class CMainWindow;
}

class CPanelWidget;
class QShortcut;

class CMainWindow : public QMainWindow, private FileListReturnPressedObserver
{
	Q_OBJECT

public:
	explicit CMainWindow(QWidget *parent = 0);
	~CMainWindow();

	static CMainWindow* get();

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

public slots:
	void updateInterface();

protected:
	void closeEvent(QCloseEvent * e) override;

private slots: // For UI
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
	void openTerminal();
	void showRecycleBInContextMenu(QPoint pos);
	void toggleQuickView();
	void currentItemChanged(Panel p, qulonglong itemHash);

// Command line
	// true if command was executed
	bool executeCommand(QString commandLineText);
	void selectPreviousCommandInTheCommandLine();
	void clearCommandLineAndRestoreFocus();
	void pasteCurrentFileName();
	void pasteCurrentFilePath();

// Main menu
	void refresh();
	void showHiddenFiles();
	void showAllFilesFromCurrentFolderAndBelow();
	void openSettingsDialog();
	void calculateOccupiedSpace();

// Settings
	void settingsChanged();

// Focus management
	void focusChanged(QWidget * old, QWidget * now);

private:
	void createToolMenuEntries(std::vector<CPluginProxy::MenuTree> menuEntries);
	void addToolMenuEntriesRecursively(CPluginProxy::MenuTree entry, QMenu* toolMenu);

	// For command line handling
	bool fileListReturnPressed() override;

	// Quick view
	void quickViewCurrentFile();

	// Helper functions
	static bool widgetBelongsToHierarchy(QWidget * const widget, QObject * const hierarchy);

	// Other
	void currentPanelChanged(QStackedWidget * panel);

private:
	Ui::CMainWindow              * ui;
	static CMainWindow*            _instance;
	std::unique_ptr<CController>   _controller;
	CPanelWidget                 * _currentFileList;
	CPanelWidget                 * _otherFileList;
	QStackedWidget               * _currentPanelWidget;
	QStackedWidget               * _otherPanelWidget;

	std::vector<std::shared_ptr<QShortcut> > _shortcuts;

	QCompleter                     _commandLineCompleter;

	bool                           _quickViewActive;
};

#endif // CMAINWINDOW_H

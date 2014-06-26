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

	void copyFiles(const std::vector<CFileSystemObject>& files, const QString& destDir);
	void moveFiles(const std::vector<CFileSystemObject>& files, const QString& destDir);

signals:
	// Is used to close all child windows
	void closed();

public slots:
	void updateInterface();

protected:
	void closeEvent(QCloseEvent * e) override;
	bool eventFilter(QObject * watched, QEvent * event) override;

private slots: // For UI
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void backSpacePressed(CPanelWidget * widget);
	void stepBackRequested(CPanelWidget * panel);
	void stepForwardRequested(CPanelWidget * panel);
	void currentPanelChanged(CPanelWidget * panel);
	void folderPathSet(QString path, const CPanelWidget * panel);
	void splitterContextMenuRequested(QPoint pos);

// File operations UI slots
	void copySelectedFiles();
	void moveSelectedFiles();
	void deleteFiles();
	void deleteFilesIrrevocably();
	void createFolder();
	void createFile();
	void itemNameEdited(Panel panel, qulonglong hash, QString newName);

// Other UI commands
	void viewFile();
	void editFile();
	void openTerminal();
	void showRecycleBInContextMenu(QPoint pos);

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

// Settings
	void settingsChanged();

private:
	void createToolMenuEntries(std::vector<CPluginProxy::MenuTree> menuEntries);
	void addToolMenuEntriesRecursively(CPluginProxy::MenuTree entry, QMenu* toolMenu);

	// For command line handling
	bool fileListReturnPressed() override;

private:
	Ui::CMainWindow              * ui;
	static CMainWindow*           _instance;
	std::shared_ptr<CController>  _controller;
	CPanelWidget                 * _currentPanel;
	CPanelWidget                 * _otherPanel;

	std::vector<std::shared_ptr<QShortcut> > _shortcuts;

	QCompleter                     _commandLineCompleter;
};

#endif // CMAINWINDOW_H

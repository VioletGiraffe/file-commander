 #ifndef CMAINWINDOW_H
#define CMAINWINDOW_H

#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "panel/cpanelwidget.h"

#include "QtAppIncludes"

#include <vector>
#include <memory>

namespace Ui {
class CMainWindow;
}

class CPanelWidget;
class QShortcut;

class CMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit CMainWindow(QWidget *parent = 0);
	~CMainWindow();
	void initButtons();
	void initActions();

	// For manual focus management
	void tabKeyPressed ();

signals:
	// Is used to close all child windows
	void closed();

public slots:
	void updateInterface();

protected:
	virtual void closeEvent(QCloseEvent * e);

private slots: // For UI
	void itemActivated(qulonglong hash, CPanelWidget * panel);
	void backSpacePressed(CPanelWidget * widget);
	void stepBackRequested(CPanelWidget * panel);
	void stepForwardRequested(CPanelWidget * panel);
	void currentPanelChanged(CPanelWidget * panel);
	void folderPathSet(QString path, const CPanelWidget * panel);
	void splitterContextMenuRequested(QPoint pos);

// File operations UI slots
	void copyFiles();
	void moveFiles();
	void deleteFiles();
	void deleteFilesIrrevocably();
	void createFolder();
	void createFile();

// Other UI commands
	void viewFile();
	void editFile();
	void openTerminal();
	void showRecycleBInContextMenu(QPoint pos);
	void executeCommand();

// Main menu
	void showAllFilesFromCurrentFolderAndBelow();
	void openSettingsDialog();

// Settings
	void settingsChanged();

// Command line
	void processError(QProcess::ProcessError error);

private:
	Ui::CMainWindow * ui;
	CController     & _controller;
	CPanelWidget    * _currentPanel;
	CPanelWidget    * _otherPanel;

	std::vector<std::shared_ptr<QShortcut> > _shortcuts;
};

#endif // CMAINWINDOW_H

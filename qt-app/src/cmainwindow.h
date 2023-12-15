#pragma once

#include "cfilesystemobject.h"
#include "ccontroller.h"
#include "panel/filelistwidget/cfilelistview.h"
#include "panel/cpaneldisplaycontroller.h"
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

class CMainWindow final : public QMainWindow,
		private FileListReturnPressedObserver,
		private PanelContentsChangedListener
{
public:
	explicit CMainWindow(QWidget* parent = nullptr) noexcept;
	~CMainWindow() noexcept;
	[[nodiscard]] static CMainWindow* get();

	// One-time initialization
	[[nodiscard]] bool created() const;
	void onCreate();

	void updateInterface();

	void initButtons();
	void initActions();

	// For manual focus management
	void tabKeyPressed();

	bool copyFiles(std::vector<CFileSystemObject>&& files, const QString& destDir);
	bool moveFiles(std::vector<CFileSystemObject>&& files, const QString& destDir);

protected:
	void closeEvent(QCloseEvent * e) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
// UI slots
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
	void filterItemsByName();
	void currentItemChanged(Panel p, qulonglong itemHash);

	void toggleFullScreenMode(bool fullscreen);
	void toggleTabletMode(bool tabletMode);

// Command line
	// true if command was executed
	bool executeCommand(const QString& commandLineText);
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
	void calculateEachFolderSize();
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
	void initCore();

	void createToolMenuEntries(const std::vector<CPluginProxy::MenuTree>& menuEntries);
	void addToolMenuEntriesRecursively(const CPluginProxy::MenuTree& entry, QMenu* toolMenu);

	CPanelDisplayController& currentPanelDisplayController();
	CPanelDisplayController& otherPanelDisplayController();

	// For command line handling
	bool fileListReturnPressed() override;

	// Quick view
	void quickViewCurrentFile();

	// Other
	void currentPanelChanged(Panel panel);

	// Timer
	void uiThreadTimerTick();

	// Window title management (#143)
	void updateWindowTitleWithCurrentFolderNames();

private:
	Ui::CMainWindow* ui;
	static CMainWindow* _instance;

	QTimer* _uiThreadTimer = nullptr;

	std::unique_ptr<CController> _controller;
	CPanelWidget* _currentFileList = nullptr;
	CPanelWidget* _otherFileList = nullptr;
	CPanelDisplayController _leftPanelDisplayController, _rightPanelDisplayController;

	QCompleter _commandLineCompleter;

	enum { NormalWindow, MaximizedWindow } _windowStateBeforeFullscreen = NormalWindow;
};


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

#include <memory>
#include <vector>

namespace Ui {
	class CMainWindow;
}

class CPanelWidget;
class CFileOperationDialog;
enum class TransferKind; // fileoperations/fileoperationtypes.h
class QShortcut;
class QStackedWidget;
class QPushButton;

class CMainWindow final : public QMainWindow,
		private FileListReturnPressedObserver,
		private PanelContentsChangedListener
{
public:
	explicit CMainWindow(QWidget* parent = nullptr) noexcept;
	~CMainWindow() noexcept;
	[[nodiscard]] static CMainWindow* get();

	void updateInterface();

	void initButtons();
	void initActions();

	// For manual focus management
	void tabKeyPressed();

	// The transfer launch path (F5/F6, menu/toolbar, and drag-and-drop) over the operation engine: it converts
	// the panel selection and edited destination into a typed request and drives one CFileOperationDialog.
	bool launchFileTransfer(TransferKind kind, std::vector<CFileSystemObject>&& sources, const QString& destinationDirectory);

	// Bottom left point
	QPoint nextBackgroundDialogPosition() const;

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
	// Shared by both delete slots: routes to the platform backend (native trash/shell) or the internal dialog.
	void performDeletion(bool toTrash);
	void createFolder();
	void createFile();

// Selection slots
	void invertSelection();

// Other UI commands
	void viewFile();
	void viewFileInTextViewer();
	void editFile();
	void showRecycleBInContextMenu(QPoint pos);
	// Swaps the bottom command buttons' captions between their normal and Shift-modified variants
	void setShiftCaptions(bool shifted);
	[[nodiscard]] static bool shiftKeyHeld();
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
	void calculateStatistics();
	void calculateEachFolderSize();
	void checkForUpdates();
	void reportBug();
	void about();

// Settings
	void settingsChanged();

// Focus management
	void focusChanged(QWidget * old, QWidget * now);

private:
	void onPanelContentsChanged(Panel p, FileListRefreshCause operation) override;

private:
	void initCore();

	void createToolMenuEntries(QMenu* menu, const std::vector<CPluginProxy::MenuTree>& menuEntries);
	void addToolMenuEntriesRecursively(const CPluginProxy::MenuTree& entry, QMenu* parentMenu);

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

	void registerFileOperationDialog(CFileOperationDialog* dialog);
	void onFileDialogFinished(QObject* object);

private:
	Ui::CMainWindow* ui;
	static CMainWindow* _instance;

	QTimer* _uiThreadTimer = nullptr;
	QTimer* _historyAutosaveTimer = nullptr; // Periodically persists the visited-folders history so an abrupt exit doesn't lose it

	std::unique_ptr<CController> _controller;
	CPanelWidget* _currentFileList = nullptr;
	CPanelWidget* _otherFileList = nullptr;
	CPanelDisplayController _leftPanelDisplayController, _rightPanelDisplayController;

	QCompleter _commandLineCompleter;

	enum { NormalWindow, MaximizedWindow } _windowStateBeforeFullscreen = NormalWindow;

	std::vector<CFileOperationDialog*> _activeFileOperationDialogs;

	// Bottom command buttons whose caption reflects the Shift-modified action while Shift is held
	struct ShiftCaption { QPushButton* button; QString normal; QString shifted; };
	std::vector<ShiftCaption> _shiftCaptions;
};


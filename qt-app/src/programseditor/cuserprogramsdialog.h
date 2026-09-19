#pragma once

#include "userprograms/userprograms.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Edits the Programs menu entries. Each list row stores its program's fields, so reordering needs no other bookkeeping.
class CUserProgramsDialog final : public QDialog
{
public:
	using RunProgram = std::function<void(const UserProgram&)>;

	// The preview expands placeholders against `panelState`
	CUserProgramsDialog(const std::vector<UserProgram>& programs, PlaceholderValues panelState, RunProgram runProgram, QWidget* parent);

	[[nodiscard]] std::vector<UserProgram> programs() const;

	void accept() override;

protected:
	// A dropped file becomes a new program
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	[[nodiscard]] QWidget* createListPane();
	[[nodiscard]] QWidget* createDetailsPane();

	QTreeWidgetItem* insertProgram(const UserProgram& program, int row);
	void addNewProgram();
	void duplicateCurrentProgram();
	void removeCurrentProgram();
	void moveCurrentProgram(int offset);

	void showDetails(QTreeWidgetItem* item);
	[[nodiscard]] UserProgram programInDetails() const;
	void storeDetails();
	void updateCurrentIcon();
	void updatePreview();
	void updateHotkeys();
	void updateEnabledState();

	void browseForProgram();
	void browseForWorkingDir();
	void insertIntoCommandLine(const QString& text);

private:
	const PlaceholderValues _panelState;
	const RunProgram _runProgram;
	bool _showingDetails = false; // Filling the detail widgets must not store them back

	QTreeWidget* _list = nullptr;
	QPushButton* _btnDuplicate = nullptr;
	QPushButton* _btnRemove = nullptr;
	QToolButton* _btnMoveUp = nullptr;
	QToolButton* _btnMoveDown = nullptr;

	QWidget* _details = nullptr;
	QLineEdit* _name = nullptr;
	QLineEdit* _commandLine = nullptr;
	QLabel* _preview = nullptr;
	QComboBox* _workingDir = nullptr;
	QLineEdit* _customWorkingDir = nullptr;
	QToolButton* _btnBrowseWorkingDir = nullptr;
	QCheckBox* _editBeforeRunning = nullptr;
	QLabel* _hotkey = nullptr;
	QPushButton* _btnTestRun = nullptr;
};

#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/batchrename.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

#include <utility>
#include <vector>

class CHistoryComboBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Renames the given items by rules, previewing every new name as the rules are edited
class CBatchRenameDialog final : public QDialog
{
public:
	CBatchRenameDialog(QWidget* parent, const std::vector<CFileSystemObject>& items);
	~CBatchRenameDialog() override;

	void reject() override;

protected:
	void closeEvent(QCloseEvent* e) override;

private:
	struct Row
	{
		CFileSystemObject item;
		CEntryPath path;
		QString newName;
		QString outcome; // The last renaming's result, shown until the rules change
		bool outcomeIsError = false;
	};

	using RowRenames = std::vector<std::pair<size_t, QString>>; // Row index and name

	[[nodiscard]] BatchRenameRules rules() const;
	void rulesChanged();
	void updatePreview();
	void applyRenaming();
	void undoRenaming();
	// Returns the renamed rows with their previous names
	RowRenames renameRows(const RowRenames& renames);
	void setRenamingInProgress(bool inProgress);

private:
	std::vector<Row> _rows; // In panel order: the counter follows it
	RowRenames _undo;
	bool _showingOutcomes = false;
	bool _renaming = false;
	bool _cancelRequested = false;

	QWidget* _rulesPanel = nullptr;
	CHistoryComboBox* _nameMask = nullptr;
	CHistoryComboBox* _extensionMask = nullptr;
	CHistoryComboBox* _find = nullptr;
	CHistoryComboBox* _replaceWith = nullptr;
	QCheckBox* _findIsRegex = nullptr;
	QCheckBox* _findIsCaseSensitive = nullptr;
	QLabel* _findError = nullptr;
	QComboBox* _letterCase = nullptr;
	QSpinBox* _counterStart = nullptr;
	QSpinBox* _counterStep = nullptr;
	QSpinBox* _counterDigits = nullptr;
	QTableWidget* _preview = nullptr;
	QPushButton* _applyButton = nullptr;
	QPushButton* _undoButton = nullptr;
	QPushButton* _closeButton = nullptr;
};

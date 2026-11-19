#pragma once

#include "batchrename/cbatchrenamesession.h"
#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/batchrename.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

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

private:
	[[nodiscard]] BatchRenameRules rules() const;
	void rulesChanged();
	void showSession();
	void applyRenaming();
	void runStep(CBatchRenameSession::Step step);

private:
	CBatchRenameSession _session; // Items in panel order: the counter follows it

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
};

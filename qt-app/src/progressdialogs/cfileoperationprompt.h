#pragma once

#include "fileoperations/fileoperationtypes.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFileOperationPrompt;
}

class QPushButton;

// The operation whose issue is being presented; selects consequence wording only
// (e.g. "Make writable and move" versus "and delete"), never the available policy.
enum class PromptOperation
{
	Copy,
	Move,
	Delete
};

// The typed decision prompt: renders one DecisionRequest - only the delivered allowed actions, the
// remaining-matching-issues scope where permitted and meaningful, and a rename row when Rename is
// offered - and returns one Decision. It knows nothing of the executors' retry protocol and never
// talks to the job.
class CFileOperationPrompt final : public QDialog
{
public:
	CFileOperationPrompt(const DecisionRequest& request, PromptOperation operation, QWidget* parent = nullptr);
	~CFileOperationPrompt() override;

	CFileOperationPrompt(const CFileOperationPrompt&) = delete;
	CFileOperationPrompt& operator=(const CFileOperationPrompt&) = delete;

	// Runs the modal loop. Closing the dialog by any path that is not an action button is a
	// { Cancel, ThisItem } decision.
	[[nodiscard]] Decision ask();

private:
	void onActionChosen(DecisionAction action);
	void updateRenameControls();
	// Explains in a message box why the typed name cannot be used, and returns whether it can be.
	[[nodiscard]] bool renameNameAccepted();

	void createActionButtons();
	void setInitialFocus();
	void setupEntryInfo();
	void setupAuxiliaryTexts();

	[[nodiscard]] QString questionText() const;
	[[nodiscard]] QString actionLabel(DecisionAction action) const;
	[[nodiscard]] QString scopeLabel() const;

	Ui::CFileOperationPrompt* ui;
	const DecisionRequest _request;
	const PromptOperation _operation;
	QPushButton* _renameButton = nullptr; // Only when Rename is offered; always enabled, so a refusal can be explained
	QPushButton* _defaultButton = nullptr; // The kind's Enter action; null where Enter must do nothing at all
	Decision _decision{ .action = DecisionAction::Cancel, .scope = DecisionScope::ThisItem, .newName = {} };
};

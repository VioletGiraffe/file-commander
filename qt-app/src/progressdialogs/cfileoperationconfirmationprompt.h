#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/fileoperationtypes.h"


DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFileOperationConfirmationPrompt;
}

class CFileOperationConfirmationPrompt final : public QDialog
{
public:
	// offerLinkTargets: shows the option to copy what links point to; only a copy has it
	CFileOperationConfirmationPrompt(const QString& caption, const QString& labelText, const QString& editText, bool offerLinkTargets, QWidget *parent = nullptr);
	~CFileOperationConfirmationPrompt() override;

	[[nodiscard]] QString text() const;
	[[nodiscard]] LinkTransferMode linkMode() const;

private:
	Ui::CFileOperationConfirmationPrompt *ui;
};

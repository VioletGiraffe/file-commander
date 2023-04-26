#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFileOperationConfirmationPrompt;
}

class CFileOperationConfirmationPrompt final : public QDialog
{
public:
	explicit CFileOperationConfirmationPrompt(const QString & caption, const QString& labelText, const QString& editText, QWidget *parent = nullptr);
	~CFileOperationConfirmationPrompt() override;

	[[nodiscard]] QString text() const;

private:
	Ui::CFileOperationConfirmationPrompt *ui;
};

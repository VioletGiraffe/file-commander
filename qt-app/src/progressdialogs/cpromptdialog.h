#pragma once

#include "fileoperations/operationcodes.h"
#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CPromptDialog;
}

class CPromptDialog final : public QDialog
{
public:
	CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason, const CFileSystemObject& source, const CFileSystemObject& dest = {}, const QString& message = {});
	~CPromptDialog() override;

	[[nodiscard]] UserResponse ask();
	[[nodiscard]] QString newName() const;

protected:
	void showEvent(QShowEvent * e) override;
	void hideEvent(QHideEvent * e) override;

private: // slots
	void onSkipClicked();
	void onSkipAllClicked();
	void onRenameClicked();
	void onProceedClicked();
	void onProceedAllClicked();
	void onRetryClicked();
	void onCancelClicked();

private:
	Ui::CPromptDialog *ui;
	UserResponse       _response;
	QString            _srcFileName;
	QString            _newName;
};

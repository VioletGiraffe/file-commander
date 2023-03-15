#pragma once

#include "fileoperations/operationcodes.h"
#include "cfilesystemobject.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CPromptDialog;
}

class CPromptDialog : public QDialog
{
public:
	explicit CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason, const CFileSystemObject& source, const CFileSystemObject& dest = CFileSystemObject(), const QString& message = QString());
	~CPromptDialog();

	UserResponse ask();
	QString newName() const;

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

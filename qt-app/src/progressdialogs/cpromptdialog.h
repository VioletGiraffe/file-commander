#ifndef CPROMPTDIALOG_H
#define CPROMPTDIALOG_H

#include "fileoperations/operationcodes.h"
#include "cfilesystemobject.h"
#include "../QtAppIncludes"

namespace Ui {
class CPromptDialog;
}

class CPromptDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason, const CFileSystemObject& source, const CFileSystemObject& dest = CFileSystemObject());
	~CPromptDialog();

	UserResponse ask();
	QString newName() const;

private slots:
	void onSkipClicked();
	void onSkipAllClicked();
	void onRenameClicked();
	void onProceedClicked();
	void onProceedAllClicked();
	void onCancelClicked();

private:
	Ui::CPromptDialog *ui;
	UserResponse       _response;
	QString            _srcFileName;
	QString            _newName;
};

#endif // CPROMPTDIALOG_H

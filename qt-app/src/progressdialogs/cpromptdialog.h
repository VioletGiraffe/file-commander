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
	void on_btnSkip_clicked();
	void on_btnSkipAll_clicked();
	void on_btnRename_clicked();
	void on_btnProceed_clicked();
	void on_btnProceedAll_clicked();
	void on_btnCancel_clicked();

private:
	Ui::CPromptDialog *ui;
	UserResponse       _response;
	QString            _srcFileName;
	QString            _newName;
};

#endif // CPROMPTDIALOG_H

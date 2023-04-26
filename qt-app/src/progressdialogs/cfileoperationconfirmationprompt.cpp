#include "cfileoperationconfirmationprompt.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfileoperationconfirmationprompt.h"
RESTORE_COMPILER_WARNINGS

CFileOperationConfirmationPrompt::CFileOperationConfirmationPrompt(const QString& caption, const QString& labelText, const QString& editText, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CFileOperationConfirmationPrompt)
{
	ui->setupUi(this);
	ui->_label->setText(labelText);
	ui->_editField->setText(editText);
	ui->_editField->selectAll();
	setWindowTitle(caption);
}

CFileOperationConfirmationPrompt::~CFileOperationConfirmationPrompt()
{
	delete ui;
}

QString CFileOperationConfirmationPrompt::text() const
{
	return ui->_editField->text();
}

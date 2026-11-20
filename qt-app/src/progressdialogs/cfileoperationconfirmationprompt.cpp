#include "cfileoperationconfirmationprompt.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfileoperationconfirmationprompt.h"
RESTORE_COMPILER_WARNINGS

CFileOperationConfirmationPrompt::CFileOperationConfirmationPrompt(const QString& caption, const QString& labelText, const QString& editText,
	const bool offerLinkTargets, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CFileOperationConfirmationPrompt)
{
	ui->setupUi(this);
	ui->_label->setText(labelText);
	ui->_editField->setText(editText);
	ui->_editField->selectAll();
	ui->_copyLinkTargets->setVisible(offerLinkTargets);
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

LinkTransferMode CFileOperationConfirmationPrompt::linkMode() const
{
	return ui->_copyLinkTargets->isChecked() ? LinkTransferMode::CopyTargets : LinkTransferMode::CopyLinks;
}

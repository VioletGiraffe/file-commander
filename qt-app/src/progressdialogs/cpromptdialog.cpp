#include "cpromptdialog.h"
#include "ui_cpromptdialog.h"

CPromptDialog::CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason, const CFileSystemObject& source, const CFileSystemObject& dest /*= CFileSystemObject()*/) :
	QDialog(parent),
	ui(new Ui::CPromptDialog),
	_response(urNone)
{
	ui->setupUi(this);

	connect(ui->btnCancel, SIGNAL(clicked()), SLOT(on_btnCancel_clicked()));
	connect(ui->btnCancelDeletion, SIGNAL(clicked()), SLOT(on_btnCancel_clicked()));
	connect(ui->btnDelete, SIGNAL(clicked()), SLOT(on_btnProceed_clicked()));
	connect(ui->btnDeleteAll, SIGNAL(clicked()), SLOT(on_btnProceedAll_clicked()));
	connect(ui->btnOverwrite, SIGNAL(clicked()), SLOT(on_btnProceed_clicked()));
	connect(ui->btnOverwriteAll, SIGNAL(clicked()), SLOT(on_btnProceedAll_clicked()));
	connect(ui->btnRename, SIGNAL(clicked()), SLOT(on_btnRename_clicked()));
	connect(ui->btnSkip, SIGNAL(clicked()), SLOT(on_btnSkip_clicked()));
	connect(ui->btnSkipAll, SIGNAL(clicked()), SLOT(on_btnSkipAll_clicked()));
	connect(ui->btnSkipDeletion, SIGNAL(clicked()), SLOT(on_btnSkip_clicked()));
	connect(ui->btnSkipAllDeletion, SIGNAL(clicked()), SLOT(on_btnSkipAll_clicked()));

	switch (promptReason)
	{
	case hrFileExists:
		ui->lblQuestion->setText("File already exists. What do you want to do?");
		break;
	case hrSourceFileIsReadOnly:
		ui->lblQuestion->setText("The source file is read-only. What do you want to do?");
		break;
	case hrDestFileIsReadOnly:
		ui->lblQuestion->setText("The destination file is read-only. What do you want to do?");
		break;
	case hrFileDoesntExit:
		ui->lblQuestion->setText("The file doesn't exist. What do you want to do?");
		break;
	case hrUnknownError:
		ui->lblQuestion->setText("An unknown error occurred. What do you want to do?");
		break;
	default:
		ui->lblQuestion->setText("An unknown error occurred. What do you want to do?");
		break;
	}

	if (op == operationDelete)
	{
		ui->stackedWidget->setCurrentIndex(1);

		ui->lblSize->setText(fileSizeToString(source.size()));
		QDateTime modificationDate;
		modificationDate.setTime_t((uint)source.properties().modificationDate);
		modificationDate = modificationDate.toLocalTime();
		ui->lblModTime->setText(modificationDate.toString("dd.MM.yyyy hh:mm"));
	}
	else
	{
		ui->stackedWidget->setCurrentIndex(0);

		ui->lblSourceSize->setText(fileSizeToString(source.size()));
		QDateTime modificationDate;
		modificationDate.setTime_t((uint)source.properties().modificationDate);
		modificationDate = modificationDate.toLocalTime();
		ui->lblSourceModTime->setText(modificationDate.toString("dd.MM.yyyy hh:mm"));

		ui->lblDestSize->setText(fileSizeToString(dest.size()));
		modificationDate.setTime_t((uint)dest.properties().modificationDate);
		modificationDate = modificationDate.toLocalTime();
		ui->lblDestModTime->setText(modificationDate.toString("dd.MM.yyyy hh:mm"));
	}

	_srcFileName = source.fileName();
}

UserResponse CPromptDialog::ask()
{
	exec();
	if (_response == urNone)
		_response = urAbort;
	return _response;
}

QString CPromptDialog::newName() const
{
	return _newName;
}

CPromptDialog::~CPromptDialog()
{
	delete ui;
}

void CPromptDialog::on_btnSkip_clicked()
{
	_response = urSkipThis;
	close();
}

void CPromptDialog::on_btnSkipAll_clicked()
{
	_response = urSkipAll;
	close();
}

void CPromptDialog::on_btnRename_clicked()
{
	_newName = QInputDialog::getText(this, "Rename the file", "Enter the new name for this file", QLineEdit::Normal, _srcFileName);
	if (!_newName.isEmpty())
	{
		_response = urRename;
		close();
	}
}

void CPromptDialog::on_btnProceed_clicked()
{
	_response = urProceedWithThis;
	close();
}

void CPromptDialog::on_btnProceedAll_clicked()
{
	_response = urProceedWithAll;
	close();
}

void CPromptDialog::on_btnCancel_clicked()
{
	_response = urAbort;
	close();
}

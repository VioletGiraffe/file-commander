#include "cpromptdialog.h"
#include "ui_cpromptdialog.h"
#include "filesystemhelperfunctions.h"

CPromptDialog::CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason, const CFileSystemObject& source, const CFileSystemObject& dest /*= CFileSystemObject()*/) :
	QDialog(parent),
	ui(new Ui::CPromptDialog),
	_response(urNone)
{
	ui->setupUi(this);

	connect(ui->btnCancel, SIGNAL(clicked()), SLOT(onCancelClicked()));
	connect(ui->btnCancelDeletion, SIGNAL(clicked()), SLOT(onCancelClicked()));
	connect(ui->btnDelete, SIGNAL(clicked()), SLOT(onProceedClicked()));
	connect(ui->btnDeleteAll, SIGNAL(clicked()), SLOT(onProceedAllClicked()));
	connect(ui->btnOverwrite, SIGNAL(clicked()), SLOT(onProceedClicked()));
	connect(ui->btnOverwriteAll, SIGNAL(clicked()), SLOT(onProceedAllClicked()));
	connect(ui->btnRename, SIGNAL(clicked()), SLOT(onRenameClicked()));
	connect(ui->btnSkip, SIGNAL(clicked()), SLOT(onSkipClicked()));
	connect(ui->btnSkipAll, SIGNAL(clicked()), SLOT(onSkipAllClicked()));
	connect(ui->btnSkipDeletion, SIGNAL(clicked()), SLOT(onSkipClicked()));
	connect(ui->btnSkipAllDeletion, SIGNAL(clicked()), SLOT(onSkipAllClicked()));

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

		ui->lblSrcFile->setText(source.fullAbsolutePath());
		ui->lblSourceSize->setText(fileSizeToString(source.size()));
		QDateTime modificationDate;
		modificationDate.setTime_t((uint)source.properties().modificationDate);
		modificationDate = modificationDate.toLocalTime();
		ui->lblSourceModTime->setText(modificationDate.toString("dd.MM.yyyy hh:mm"));

		ui->lblDstFile->setText(dest.fullAbsolutePath());
		ui->lblDestSize->setText(fileSizeToString(dest.size()));
		modificationDate.setTime_t((uint)dest.properties().modificationDate);
		modificationDate = modificationDate.toLocalTime();
		ui->lblDestModTime->setText(modificationDate.toString("dd.MM.yyyy hh:mm"));
	}

	_srcFileName = source.fullName();
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

void CPromptDialog::onSkipClicked()
{
	_response = urSkipThis;
	close();
}

void CPromptDialog::onSkipAllClicked()
{
	_response = urSkipAll;
	close();
}

void CPromptDialog::onRenameClicked()
{
	_newName = QInputDialog::getText(this, "Rename the file", "Enter the new name for this file", QLineEdit::Normal, _srcFileName);
	if (!_newName.isEmpty())
	{
		_response = urRename;
		close();
	}
}

void CPromptDialog::onProceedClicked()
{
	_response = urProceedWithThis;
	close();
}

void CPromptDialog::onProceedAllClicked()
{
	_response = urProceedWithAll;
	close();
}

void CPromptDialog::onCancelClicked()
{
	_response = urAbort;
	close();
}

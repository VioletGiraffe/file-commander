#include "cpromptdialog.h"
#include "filesystemhelperfunctions.h"
#include "widgets/widgetutils.h"
#include "settings/csettings.h"
#include "settings.h"

DISABLE_COMPILER_WARNINGS
#include "qtcore_helpers/qdatetime_helpers.hpp"

#include "ui_cpromptdialog.h"

#include <QInputDialog>
#include <QLineEdit>
RESTORE_COMPILER_WARNINGS

CPromptDialog::CPromptDialog(QWidget *parent, Operation op, HaltReason promptReason,
	const CFileSystemObject& source, const CFileSystemObject& dest, const QString& message) :

	QDialog(parent),
	ui(new Ui::CPromptDialog),
	_response(urNone)
{
	ui->setupUi(this);

	connect(ui->btnCancel,          &QPushButton::clicked, this, &CPromptDialog::onCancelClicked);
	connect(ui->btnCancelDeletion,  &QPushButton::clicked, this, &CPromptDialog::onCancelClicked);
	connect(ui->btnDeleteAnyway,    &QPushButton::clicked, this, &CPromptDialog::onProceedClicked);
	connect(ui->btnDeleteAllAnyway, &QPushButton::clicked, this, &CPromptDialog::onProceedAllClicked);
	connect(ui->btnOverwrite,       &QPushButton::clicked, this, &CPromptDialog::onProceedClicked);
	connect(ui->btnOverwriteAll,    &QPushButton::clicked, this, &CPromptDialog::onProceedAllClicked);
	connect(ui->btnRename,          &QPushButton::clicked, this, &CPromptDialog::onRenameClicked);
	connect(ui->btnSkip,            &QPushButton::clicked, this, &CPromptDialog::onSkipClicked);
	connect(ui->btnSkipAll,         &QPushButton::clicked, this, &CPromptDialog::onSkipAllClicked);
	connect(ui->btnSkipDeletion,    &QPushButton::clicked, this, &CPromptDialog::onSkipClicked);
	connect(ui->btnSkipAllDeletion, &QPushButton::clicked, this, &CPromptDialog::onSkipAllClicked);
	connect(ui->btnRetry,           &QPushButton::clicked, this, &CPromptDialog::onRetryClicked);

	switch (promptReason)
	{
	case hrFileExists:
		ui->lblQuestion->setText(tr("File or folder already exists."));
		break;
	case hrSourceFileIsReadOnly:
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnRename->setVisible(false);
		ui->lblQuestion->setText(tr("The source file or folder is read-only."));
		break;
	case hrDestFileIsReadOnly:
		ui->lblQuestion->setText(tr("The destination file or folder is read-only."));
		break;
	case hrFailedToMakeItemWritable:
		ui->lblQuestion->setText(tr("Failed to make the file or folder writable."));
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnRename->setVisible(false);
		break;
	case hrFileDoesntExit:
		ui->lblQuestion->setText(tr("The file or folder doesn't exist."));
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnRename->setVisible(false);
		ui->btnDeleteAllAnyway->setVisible(false);
		ui->btnDeleteAnyway->setVisible(false);
		break;
	case hrCreatingFolderFailed:
		ui->lblQuestion->setText(tr("Failed to create the folder\n%1").arg(source.fullAbsolutePath()));
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnRename->setVisible(false);
		break;
	case hrFailedToDelete:
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnDeleteAnyway->setVisible(false);
		ui->btnDeleteAllAnyway->setVisible(false);
		ui->btnRename->setVisible(false);
		ui->lblQuestion->setText(tr("Failed to delete\n%1").arg(source.fullAbsolutePath()));
		break;
	case hrNotEnoughSpace:
		ui->lblQuestion->setText(tr("There is not enough space on the destination storage. What do you want to do?"));
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnDeleteAnyway->setVisible(false);
		ui->btnDeleteAllAnyway->setVisible(false);
		ui->btnRename->setVisible(false);
		break;
	case hrUnknownError:
		ui->lblQuestion->setText(tr("An unknown error occurred. What do you want to do?"));
		ui->btnOverwrite->setVisible(false);
		ui->btnOverwriteAll->setVisible(false);
		ui->btnRename->setVisible(false);
		break;
	default:
		ui->lblQuestion->setText(tr("An unknown error occurred. What do you want to do?"));
		break;
	}

	if (!message.isEmpty())
		ui->lblQuestion->setText(ui->lblQuestion->text() + "\n\n" + message);

	if (op == operationDelete || promptReason == hrSourceFileIsReadOnly || promptReason == hrFailedToMakeItemWritable)
	{
		ui->stackedWidget->setCurrentIndex(1);

		ui->m_lblItemBeingDeleted->setText(source.fullAbsolutePath());
		ui->lblSize->setText(fileSizeToString(source.size()));
		auto modificationDate = fromTime_t(source.properties().modificationDate).toLocalTime();
		ui->lblModTime->setText(modificationDate.toString(QSL("dd.MM.yyyy hh:mm")));
	}
	else
	{
		ui->stackedWidget->setCurrentIndex(0);

		if (source.isValid())
		{
			ui->lblSrcFile->setText(source.fullAbsolutePath());
			ui->lblSourceSize->setText(fileSizeToString(source.size()));
			auto modificationDate = fromTime_t(source.properties().modificationDate).toLocalTime();
			ui->lblSourceModTime->setText(modificationDate.toString(QSL("dd.MM.yyyy hh:mm")));
		}
		else
			WidgetUtils::setLayoutVisible(ui->sourceFileInfo, false);

		if (dest.isValid())
		{
			ui->lblDstFile->setText(dest.fullAbsolutePath());
			ui->lblDestSize->setText(fileSizeToString(dest.size()));
			auto modificationDate = fromTime_t(dest.properties().modificationDate).toLocalTime();
			ui->lblDestModTime->setText(modificationDate.toString(QSL("dd.MM.yyyy hh:mm")));
		}
		else
			WidgetUtils::setLayoutVisible(ui->destFileInfo, false);
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


void CPromptDialog::showEvent(QShowEvent * e)
{
	restoreGeometry(CSettings().value(KEY_PROMPT_DIALOG_GEOMETRY).toByteArray());

	QDialog::showEvent(e);

	// Leave width intact, but set optimum height
	auto g = geometry();
	g.setHeight(sizeHint().height());
	setGeometry(g);
}

void CPromptDialog::hideEvent(QHideEvent* e)
{
	CSettings().setValue(KEY_PROMPT_DIALOG_GEOMETRY, saveGeometry());
	QDialog::hideEvent(e);
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
	_newName = QInputDialog::getText(this, tr("Rename the file"), tr("Enter the new name for this file"), QLineEdit::Normal, _srcFileName);
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

void CPromptDialog::onRetryClicked()
{
	_response = urRetry;
	close();
}

void CPromptDialog::onCancelClicked()
{
	_response = urAbort;
	close();
}

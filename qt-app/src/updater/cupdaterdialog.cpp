#include "cupdaterdialog.h"
#include "../version.h"
#include "utils/naturalsorting/cnaturalsorterqcollator.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cupdaterdialog.h"

#include <QDebug>
#include <QMessageBox>
#include <QPushButton>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

CUpdaterDialog::CUpdaterDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_updater("https://github.com/VioletGiraffe/file-commander", VERSION_STRING)
{
	ui->setupUi(this);

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &CUpdaterDialog::applyUpdate);
	ui->buttonBox->button(QDialogButtonBox::Ok)->setText("Install");

	ui->stackedWidget->setCurrentIndex(0);
	ui->progressBar->setMaximum(0);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(false);

	_updater.setUpdateStatusListener(this);
	_updater.checkForUpdates();
}

CUpdaterDialog::~CUpdaterDialog()
{
	delete ui;
}

void CUpdaterDialog::applyUpdate()
{
	ui->progressBar->setMaximum(100);
	ui->progressBar->setValue(0);
	ui->lblPercentage->setVisible(true);
	ui->lblOperationInProgress->setText("Downloading the update...");
	ui->stackedWidget->setCurrentIndex(0);

	_updater.downloadAndInstallUpdate();
}

// If no updates are found, the changelog is empty
void CUpdaterDialog::onUpdateAvailable(CAutoUpdaterGithub::ChangeLog changelog)
{
	if (!changelog.empty())
	{
		ui->stackedWidget->setCurrentIndex(1);
		for (const auto& changelogItem: changelog)
		{
			qDebug() << changelogItem.versionChanges;
			ui->changeLogViewer->append("<b>" % changelogItem.versionString % "</b>" % '\n' % changelogItem.versionChanges % "<p></p>");
		}
	}
	else
	{
		accept();
		QMessageBox::information(this, tr("No update available"), tr("You already have the latest version of the program."));
	}
}

// percentageDownloaded >= 100.0f means the download has finished
void CUpdaterDialog::onUpdateDownloadProgress(float percentageDownloaded)
{
	ui->progressBar->setValue((int)percentageDownloaded);
	ui->lblPercentage->setText(QString::number(percentageDownloaded, 'f', 2) + " %");
}

void CUpdaterDialog::onUpdateDownloadFinished()
{
	accept();
}

void CUpdaterDialog::onUpdateError(QString errorMessage)
{
	reject();
	QMessageBox::critical(this, tr("Error checking for updates"), tr(errorMessage.toUtf8().data()));
}

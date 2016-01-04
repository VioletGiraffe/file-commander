#include "cupdaterdialog.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cupdaterdialog.h"
RESTORE_COMPILER_WARNINGS

CUpdaterDialog::CUpdaterDialog(QWidget *parent, CAutoUpdaterGithub& updater) :
	QDialog(parent),
	ui(new Ui::CUpdaterDialog),
	_updater(updater)
{
	ui->setupUi(this);

	connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

CUpdaterDialog::~CUpdaterDialog()
{
	delete ui;
}

void CUpdaterDialog::downloadProgress(int progress)
{
	if (progress < 100)
		ui->progressBar->setValue(progress);
}

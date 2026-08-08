#include "caboutdialog.h"
#include "../version.h"

#include "ui_caboutdialog.h"

CAboutDialog::CAboutDialog(QWidget *parent, const std::vector<QString>& activePluginNames) :
	QDialog(parent),
	ui(new Ui::CAboutDialog)
{
	ui->setupUi(this);

	ui->lblVersion->setText(tr("Version %1 (%2 %3), Qt version %4").arg(VERSION_STRING).arg(__DATE__).arg(__TIME__).arg(QT_VERSION_STR));

	for (const QString& plugin: activePluginNames)
		ui->pluginsList->addItem(plugin);
}

CAboutDialog::~CAboutDialog()
{
	delete ui;
}

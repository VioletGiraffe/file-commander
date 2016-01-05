#include "caboutdialog.h"
#include "../version.h"
#include "pluginengine/cpluginengine.h"

#include "ui_caboutdialog.h"

CAboutDialog::CAboutDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CAboutDialog)
{
	ui->setupUi(this);

	ui->lblVersion->setText(tr("Version %1 (%2 %3), Qt version %4").arg(VERSION_STRING).arg(__DATE__).arg(__TIME__).arg(QT_VERSION_STR));

	const auto plugins = CPluginEngine::get().activePluginNames();
	for (const QString& plugin: plugins)
		ui->pluginsList->addItem(plugin);
}

CAboutDialog::~CAboutDialog()
{
	delete ui;
}

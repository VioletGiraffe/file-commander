#include "csettingspageother.h"
#include "ui_csettingspageother.h"

#include "settings.h"
#include "settings/csettings.h"
#include "ccontroller.h"
#include "shell/cshell.h"

CSettingsPageOther::CSettingsPageOther(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageOther)
{
	ui->setupUi(this);

	CSettings s;

	const auto shellCommandAndArgs = OsShell::shellExecutable();
	QString shellCommandLine = shellCommandAndArgs.first;
	if (!shellCommandAndArgs.second.isEmpty())
		(shellCommandLine += ' ') += shellCommandAndArgs.second;

	ui->_shellCommandName->setText(s.value(KEY_OTHER_SHELL_COMMAND_NAME, shellCommandLine).toString());
	ui->_cbCheckForUpdatesAutomatically->setChecked(s.value(KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY, true).toBool());
}

CSettingsPageOther::~CSettingsPageOther()
{
	delete ui;
}

void CSettingsPageOther::acceptSettings()
{
	CSettings s;
	s.setValue(KEY_OTHER_SHELL_COMMAND_NAME, ui->_shellCommandName->text());
	s.setValue(KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY, ui->_cbCheckForUpdatesAutomatically->isChecked());
}

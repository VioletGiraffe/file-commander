#include "csettingspageother.h"

#include "settings.h"
#include "ccontroller.h"
#include "shell/cshell.h"


DISABLE_COMPILER_WARNINGS
#include "ui_csettingspageother.h"

#include <QSettings>
RESTORE_COMPILER_WARNINGS

CSettingsPageOther::CSettingsPageOther(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageOther)
{
	ui->setupUi(this);

	QSettings s;

	ui->_shellCommandName->setPlaceholderText(OsShell::defaultTerminalCommand());
	ui->_shellCommandName->setText(s.value(KEY_OTHER_TERMINAL_COMMAND).toString());
#ifdef __APPLE__
	ui->label->setText(tr("Terminal application"));
	ui->label_2->setText(tr("Leave empty for the default shown, or enter the name of another terminal application, such as iTerm."));
#endif
	ui->_cbCheckForUpdatesAutomatically->setChecked(s.value(KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY, true).toBool());
}

CSettingsPageOther::~CSettingsPageOther()
{
	delete ui;
}

void CSettingsPageOther::acceptSettings()
{
	QSettings s;
	s.setValue(KEY_OTHER_TERMINAL_COMMAND, ui->_shellCommandName->text().trimmed());
	s.setValue(KEY_OTHER_CHECK_FOR_UPDATES_AUTOMATICALLY, ui->_cbCheckForUpdatesAutomatically->isChecked());
}

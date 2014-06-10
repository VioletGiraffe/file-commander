#include "csettingspageoperations.h"
#include "ui_csettingspageoperations.h"
#include "settings/csettings.h"
#include "settings.h"

CSettingsPageOperations::CSettingsPageOperations(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageOperations)
{
	ui->setupUi(this);
	CSettings s;
	ui->_cbPromptForCopyOrMove->setChecked(s.value(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, true).toBool());
}

CSettingsPageOperations::~CSettingsPageOperations()
{
	delete ui;
}

void CSettingsPageOperations::acceptSettings()
{
	CSettings s;
	s.setValue(KEY_OPERATIONS_ASK_FOR_COPY_MOVE_CONFIRMATION, ui->_cbPromptForCopyOrMove->isChecked());
}

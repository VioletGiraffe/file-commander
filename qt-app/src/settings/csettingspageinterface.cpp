#include "csettingspageinterface.h"
#include "ui_csettingspageinterface.h"

#include "settings.h"
#include "settings/csettings.h"

CSettingsPageInterface::CSettingsPageInterface(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageInterface)
{
	ui->setupUi(this);

	CSettings s;
	ui->_cbSortingNumbersAfterLetters->setChecked(s.value(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, false).toBool());
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	CSettings s;
	s.setValue(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, ui->_cbSortingNumbersAfterLetters->isChecked());
}

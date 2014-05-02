#ifdef _WIN32
#pragma warning(push, 0) // set W0
#endif

#include "csettingspageinterface.h"
#include "ui_csettingspageinterface.h"

#include "settings.h"
#include "settings/csettings.h"

#ifdef _WIN32
#pragma warning(pop)
#endif

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

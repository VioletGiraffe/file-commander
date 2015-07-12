#include "csettingspageinterface.h"
#include "ui_csettingspageinterface.h"

#include "settings.h"
#include "settings/csettings.h"

CSettingsPageInterface::CSettingsPageInterface(QWidget *parent) :
	CSettingsPage(parent),
	ui(new Ui::CSettingsPageInterface)
{
	ui->setupUi(this);

	ui->_cbRespectLastCursorPos->setChecked(CSettings().value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, false).toBool());
	ui->_cbSortingNumbersAfterLetters->setChecked(CSettings().value(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, false).toBool());
	ui->_cbDecoratedFolderIcons->setChecked(CSettings().value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	CSettings().setValue(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, ui->_cbRespectLastCursorPos->isChecked());
	CSettings().setValue(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, ui->_cbSortingNumbersAfterLetters->isChecked());
	CSettings().setValue(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, ui->_cbDecoratedFolderIcons->isChecked());
}

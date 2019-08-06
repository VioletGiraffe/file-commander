#pragma once

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageOther;
}

class CSettingsPageOther : public CSettingsPage
{
public:
	explicit CSettingsPageOther(QWidget *parent = 0);
	~CSettingsPageOther();

	void acceptSettings() override;

private:
	Ui::CSettingsPageOther *ui;
};

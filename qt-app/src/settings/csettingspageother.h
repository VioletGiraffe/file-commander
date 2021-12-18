#pragma once

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageOther;
}

class CSettingsPageOther final : public CSettingsPage
{
public:
	explicit CSettingsPageOther(QWidget *parent = nullptr);
	~CSettingsPageOther() override;

	void acceptSettings() override;

private:
	Ui::CSettingsPageOther *ui;
};

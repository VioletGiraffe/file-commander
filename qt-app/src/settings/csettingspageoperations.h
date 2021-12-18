#pragma once

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageOperations;
}

class CSettingsPageOperations final : public CSettingsPage
{
public:
	explicit CSettingsPageOperations(QWidget *parent = nullptr);
	~CSettingsPageOperations() override;

	void acceptSettings() override;

private:
	Ui::CSettingsPageOperations *ui;
};

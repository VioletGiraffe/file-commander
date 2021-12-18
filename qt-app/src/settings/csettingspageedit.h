#pragma once

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageEdit;
}

class CSettingsPageEdit final : public CSettingsPage
{
public:
	explicit CSettingsPageEdit(QWidget *parent = nullptr);
	~CSettingsPageEdit() override;

	void acceptSettings() override;

private:
	void browseForEditor();

private:
	Ui::CSettingsPageEdit *ui;
};

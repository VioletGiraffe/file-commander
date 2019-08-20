#pragma once

#include "settingsui/csettingspage.h"

#include <memory>

class QFontDialog;
namespace Ui {
class CSettingsPageInterface;
}

class CSettingsPageInterface : public CSettingsPage
{
public:
	explicit CSettingsPageInterface(QWidget *parent = 0);
	~CSettingsPageInterface();

	void acceptSettings() override;

private:
	void updateFontInfoLabel();

private:
	std::unique_ptr<QFontDialog> _fontDialog;
	Ui::CSettingsPageInterface *ui;
};

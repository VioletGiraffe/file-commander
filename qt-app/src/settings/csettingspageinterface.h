#pragma once

#include "settingsui/csettingspage.h"

#include <memory>

class QFontDialog;
namespace Ui {
class CSettingsPageInterface;
}

class CSettingsPageInterface final : public CSettingsPage
{
public:
	explicit CSettingsPageInterface(QWidget *parent = nullptr);
	~CSettingsPageInterface() override;

	void acceptSettings() override;

private:
	void updateFontInfoLabel();

private:
	std::unique_ptr<QFontDialog> _fontDialog;
	Ui::CSettingsPageInterface *ui;
};

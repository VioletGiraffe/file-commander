#pragma once

#include "settingsui/csettingspage.h"

#include <memory>

class QFontDialog;
namespace Ui {
class CSettingsPageInterface;
}

class QAbstractButton;

class CSettingsPageInterface final : public CSettingsPage
{
public:
	explicit CSettingsPageInterface(QWidget *parent);
	~CSettingsPageInterface() override;

	void acceptSettings() override;

private:
	void updateFontInfoLabel();
	int colorSchemeFromButton(QAbstractButton *btn);

private:
	std::unique_ptr<QFontDialog> _fontDialog;
	Ui::CSettingsPageInterface *ui;

	bool _styleChanged = false;
	bool _colorSchemeChanged = false;
};

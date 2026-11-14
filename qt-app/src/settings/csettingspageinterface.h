#pragma once

// Submodule includes
#include "appdialogs/csettingspage.h"
#include "theme/cthemeselectioncontrols.h"


#include <memory>
#include <optional>

class QFontDialog;
namespace Ui {
class CSettingsPageInterface;
}

class CSettingsPageInterface final : public CSettingsPage
{
public:
	explicit CSettingsPageInterface(QWidget *parent);
	~CSettingsPageInterface() override;

	void acceptSettings() override;
	void rejectSettings() override;

private:
	void updateFontInfoLabel();

private:
	std::unique_ptr<QFontDialog> _fontDialog;
	Ui::CSettingsPageInterface *ui;

	// Empty until the form its rows go into exists
	std::optional<CThemeSelectionControls> _themeControls;
};

#pragma once

// Submodule includes
#include "appdialogs/csettingspage.h"
#include "compiler/compiler_warnings_control.h"
#include "theme/cthemecontroller.h"


DISABLE_COMPILER_WARNINGS
#include <QFont>
RESTORE_COMPILER_WARNINGS

#include <memory>

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
	// The font dialog does not hold the choice: a stylesheet change resets the font it reports
	QFont _fileListFont;
	Ui::CSettingsPageInterface *ui;

	const CThemeController::Selection _initialThemeSelection = CThemeController::instance().selection();
};

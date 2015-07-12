#ifndef CSETTINGSPAGEEDIT_H
#define CSETTINGSPAGEEDIT_H

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageEdit;
}

class CSettingsPageEdit : public CSettingsPage
{
	Q_OBJECT

public:
	explicit CSettingsPageEdit(QWidget *parent = 0);
	~CSettingsPageEdit();

	void acceptSettings() override;

private:
	void browseForEditor();

private:
	Ui::CSettingsPageEdit *ui;
};

#endif // CSETTINGSPAGEEDIT_H

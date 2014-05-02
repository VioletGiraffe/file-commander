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

	virtual void acceptSettings() override;

private slots:
	void browseForEditor();

private:
	Ui::CSettingsPageEdit *ui;
};

#endif // CSETTINGSPAGEEDIT_H

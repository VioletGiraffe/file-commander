#ifndef CSETTINGSPAGEOTHER_H
#define CSETTINGSPAGEOTHER_H

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageOther;
}

class CSettingsPageOther : public CSettingsPage
{
	Q_OBJECT

public:
	explicit CSettingsPageOther(QWidget *parent = 0);
	~CSettingsPageOther();

	virtual void acceptSettings() override;

private:
	Ui::CSettingsPageOther *ui;
};

#endif // CSETTINGSPAGEOTHER_H

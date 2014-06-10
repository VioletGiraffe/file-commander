#ifndef CSETTINGSPAGEOPERATIONS_H
#define CSETTINGSPAGEOPERATIONS_H

#include "settingsui/csettingspage.h"

namespace Ui {
class CSettingsPageOperations;
}

class CSettingsPageOperations : public CSettingsPage
{
	Q_OBJECT

public:
	explicit CSettingsPageOperations(QWidget *parent = 0);
	~CSettingsPageOperations();

	void acceptSettings() override;

private:
	Ui::CSettingsPageOperations *ui;
};

#endif // CSETTINGSPAGEOPERATIONS_H

#ifndef CSETTINGSPAGEINTERFACE_H
#define CSETTINGSPAGEINTERFACE_H

#include "csettingspage.h"

namespace Ui {
class CSettingsPageInterface;
}

class CSettingsPageInterface : public CSettingsPage
{
	Q_OBJECT

public:
	explicit CSettingsPageInterface(QWidget *parent = 0);
	~CSettingsPageInterface();

	virtual void acceptSettings() override;

private:
	Ui::CSettingsPageInterface *ui;
};

#endif // CSETTINGSPAGEINTERFACE_H

#ifndef CSETTINGSPAGE_H
#define CSETTINGSPAGE_H

#include <QWidget>

class CSettingsPage : public QWidget
{
	Q_OBJECT
public:
	explicit CSettingsPage(QWidget *parent = 0);
	virtual ~CSettingsPage();

	virtual void acceptSettings() = 0;
};

#endif // CSETTINGSPAGE_H

#ifndef CHISTORYCOMBOBOX_H
#define CHISTORYCOMBOBOX_H

#include "QtAppIncludes"

class CHistoryComboBox : public QComboBox
{
	Q_OBJECT

public:
	CHistoryComboBox(QWidget * parent = 0);

public slots:
	void currentItemActivated();
	void setHistoryMode(bool historyMode);
};

#endif // CHISTORYCOMBOBOX_H

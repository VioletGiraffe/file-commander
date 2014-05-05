#include "chistorycombobox.h"

CHistoryComboBox::CHistoryComboBox(QWidget* parent) : QComboBox(parent)
{
}

void CHistoryComboBox::currentItemActivated()
{
	//QAbstractItemModel * model = model();
	const QString item = itemText(currentIndex());
	removeItem(currentIndex());
	insertItem(0, item);
	setCurrentIndex(0);
	lineEdit()->clear();
}

void CHistoryComboBox::setHistoryMode(bool historyMode)
{
	if (historyMode)
		connect(lineEdit(), SIGNAL(returnPressed()), SLOT(currentItemActivated()), Qt::QueuedConnection);
	else
		disconnect(this, SLOT(currentItemActivated()));
}

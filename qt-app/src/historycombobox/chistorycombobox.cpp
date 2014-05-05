#include "chistorycombobox.h"

CHistoryComboBox::CHistoryComboBox(QWidget* parent) : QComboBox(parent)
{
}

// Moves the currently selected item to the top
void CHistoryComboBox::currentItemActivated()
{
	//QAbstractItemModel * model = model();
	const QString item = itemText(currentIndex());
	removeItem(currentIndex());
	insertItem(0, item);
	setCurrentIndex(0);
	lineEdit()->clear();
}

// Enables or disables history mode (moving selected item to the top)
void CHistoryComboBox::setHistoryMode(bool historyMode)
{
	if (historyMode)
		connect(lineEdit(), SIGNAL(returnPressed()), SLOT(currentItemActivated()), Qt::QueuedConnection);
	else
		disconnect(this, SLOT(currentItemActivated()));
}

// Switch to the next command following the current one
void CHistoryComboBox::cycleLastCommands()
{
	if (!lineEdit() || count() == 0)
		return;

	if (lineEdit()->text().isEmpty())
		setCurrentIndex(0);
	else if (currentIndex() < count() - 1)
		setCurrentIndex(currentIndex() + 1);

	lineEdit()->selectAll();
}

// Set current index to 0 and clear line edit
void CHistoryComboBox::reset()
{
	if (!lineEdit() || count() == 0)
		return;

	setCurrentIndex(0);
	lineEdit()->clear();
	clearFocus();
}

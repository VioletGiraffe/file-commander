#include "chistorycombobox.h"
#include <assert.h>

CHistoryComboBox::CHistoryComboBox(QWidget* parent) : QComboBox(parent)
{
}

// Moves the currently selected item to the top
void CHistoryComboBox::currentItemActivated()
{
	const QString item = itemText(currentIndex());
	removeItem(currentIndex());
	insertItem(0, item);
	setCurrentIndex(0);
	lineEdit()->clear();
	installEventFilter(this);
	lineEdit()->installEventFilter(this);
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
	if (count() <= 0)
		return;

	if (currentText().isEmpty())
		setCurrentIndex(0);
	else if (currentIndex() < count() - 1)
		setCurrentIndex(currentIndex() + 1);

	lineEdit()->selectAll(); // Causes a bug
}

// Set current index to 0 and clear line edit
void CHistoryComboBox::reset()
{
	if (!lineEdit() || count() <= 0)
		return;

	setCurrentIndex(0);
	lineEdit()->clear();
	clearFocus();
}

bool CHistoryComboBox::eventFilter(QObject * object, QEvent * e)
{
	if (!lineEdit()->hasFocus() || (e->type() != QEvent::KeyPress && e->type() != QEvent::KeyRelease))
		return false;

	// FIXME: first return press after starting the program still makes its way to lineedit somehow
	QKeyEvent * keyEvent = dynamic_cast<QKeyEvent*>(e);
	assert(e);
	if (keyEvent->key() == Qt::Key_Enter || keyEvent->key() == Qt::Key_Return)
	{
		if (e->type() != QEvent::KeyPress)
			emit lineeditReturnPressed();
		return true;
	}

	return false;
}

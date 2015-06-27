#include "chistorycombobox.h"
#include "utils/utils.h"

#include <QKeyEvent>
#include <QLineEdit>

#include <assert.h>

CHistoryComboBox::CHistoryComboBox(QWidget* parent) :
	QComboBox(parent),
	_bHistoryMode(true),
	_bClearEditorOnItemActivation(false)
{
	// without this call lineEdit is not created so it would be impossible to access it
	setEditable(true);

	installEventFilter(this);
	lineEdit()->installEventFilter(this);
}

void CHistoryComboBox::setClearEditorOnItemActivation(bool clear)
{
	_bClearEditorOnItemActivation = clear;
}

void CHistoryComboBox::setSelectPreviousItemShortcut(const QKeySequence& selectPreviousItemShortcut)
{
	_selectPreviousItemShortcut = selectPreviousItemShortcut;
}

// Enables or disables history mode (moving activated item to the top)
void CHistoryComboBox::setHistoryMode(bool historyMode)
{
	_bHistoryMode = historyMode;
}

bool CHistoryComboBox::historyMode() const
{
	return _bHistoryMode;
}

// Switch to the next combobox item (which means going back through the history if history mode is set)
void CHistoryComboBox::selectPreviousItem()
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

bool CHistoryComboBox::eventFilter(QObject*, QEvent* e)
{
	if (e->type() == QEvent::KeyPress)
	{
		QKeyEvent * keyEvent = dynamic_cast<QKeyEvent*>(e);
		assert(keyEvent);

		if (keyEvent->text().isEmpty())
			return false;

		QString modifierString;
		if (keyEvent->modifiers() & Qt::ShiftModifier)
			modifierString = "Shift+";
		if (keyEvent->modifiers() & Qt::ControlModifier)
			modifierString = "Ctrl+";
		if (keyEvent->modifiers() & Qt::AltModifier)
			modifierString = "Alt+";
		if (keyEvent->modifiers() & Qt::MetaModifier)
			modifierString = "Meta+";

		QKeySequence fullSequence(modifierString+QKeySequence(keyEvent->key()).toString());
		if (!_selectPreviousItemShortcut.isEmpty() && fullSequence == _selectPreviousItemShortcut)
		{
			selectPreviousItem();
			return true;
		}
	}

	return false;
}

QStringList CHistoryComboBox::items() const
{
	QStringList itemsList;
	for (int i = 0; i < count(); ++i)
		itemsList.push_back(itemText(i));

	return itemsList;
}

void CHistoryComboBox::keyPressEvent(QKeyEvent* e)
{
	if (lineEdit()->hasFocus() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter))
	{
		e->accept();
		currentItemActivated();
	}
	else
		QComboBox::keyPressEvent(e);
}

// Moves the currently selected item to the top
void CHistoryComboBox::currentItemActivated()
{
	const QString newItem = currentText();
	emit itemActivated(newItem);

	if (_bHistoryMode)
	{
// No longer neccessary as of Qt 5.4?..
//		removeItem(currentIndex());

		auto list = items();
		list.push_front(newItem);

		list = uniqueElements(list);
		clear();
		for (int i = list.size() - 1; i >= 0; --i)
			insertItem(0, list[i]);
		setCurrentIndex(0);

		if (_bClearEditorOnItemActivation)
			lineEdit()->clear();
	}
}

#include "cfilelistitemdelegate.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CFileListItemDelegate::CFileListItemDelegate(QObject *parent) :
	QStyledItemDelegate(parent)
{
}

void CFileListItemDelegate::setEditorData(QWidget * editor, const QModelIndex & index) const
{
	QStyledItemDelegate::setEditorData(editor, index);
	QLineEdit * lineEditor = dynamic_cast<QLineEdit*>(editor);
	assert_r(lineEditor);
	const QString itemName = lineEditor->text();
	const int dot = itemName.indexOf('.');
	if (dot != -1)
		QTimer::singleShot(0, [=]() {
		lineEditor->setSelection(0, dot);
	});
}

bool CFileListItemDelegate::eventFilter(QObject * object, QEvent * event)
{
	QWidget *editor = qobject_cast<QWidget*>(object);
	if (!editor)
		return false;

	if (event->type() == QEvent::KeyPress)
	{
		switch (static_cast<QKeyEvent *>(event)->key())
		{
		case Qt::Key_Tab:
			emit closeEditor(editor, EditNextItem);
			return true;
		case Qt::Key_Backtab:
			emit closeEditor(editor, EditPreviousItem);
			return true;
		case Qt::Key_Enter:
		case Qt::Key_Return:
			if (qobject_cast<QTextEdit *>(editor) || qobject_cast<QPlainTextEdit*>(editor))
				return false; // don't filter enter key events for QTextEdit

			// commit data
			emit commitData(editor);
			emit closeEditor(editor);
			return true;
		case Qt::Key_Escape:
			// don't commit data
			emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
			return true;
		case Qt::Key_Up:
		case Qt::Key_Down:
			// don't commit data
			emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
			// Return false so that the TreeView will handle this event and highlight the next item properly
			return false;
		default:
			return false;
		}

	}
	else if (event->type() == QEvent::FocusOut || (event->type() == QEvent::Hide && editor->isWindow()))
	{
		//the Hide event will take care of he editors that are in fact complete dialogs
		if (!editor->isActiveWindow() || (QApplication::focusWidget() != editor))
		{
			QWidget *w = QApplication::focusWidget();
			while (w)
			{ // don't worry about focus changes internally in the editor
				if (w == editor)
					return false;
				w = w->parentWidget();
			}

			emit closeEditor(editor, NoHint);
		}
	}
	else if (event->type() == QEvent::ShortcutOverride)
	{
		if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape)
		{
			event->accept();
			return true;
		}
	}
	return false;
}

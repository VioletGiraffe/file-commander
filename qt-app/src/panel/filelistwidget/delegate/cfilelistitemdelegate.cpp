#include "cfilelistitemdelegate.h"
#include "assert/advanced_assert.h"
#include "../model/cfilelistmodel.h"
#include "ccontroller.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSortFilterProxyModel>
#include <QTextEdit>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

// Item rename handling
void CFileListItemDelegate::setEditorData(QWidget * editor, const QModelIndex & index) const
{
	QStyledItemDelegate::setEditorData(editor, index);
	auto* lineEditor = dynamic_cast<QLineEdit*>(editor);
	assert_r(lineEditor);
	assert_and_return_r(index.isValid(), );

	auto* sortModel = dynamic_cast<const QSortFilterProxyModel*>(index.model());
	assert_and_return_message_r(sortModel, "Something has changed in the model hierarchy", );
	auto* model = dynamic_cast<const CFileListModel*>(sortModel->sourceModel());
	assert_and_return_message_r(model, "Something has changed in the model hierarchy", );
	auto hash = model->itemHash(sortModel->mapToSource(index));
	const auto item = CController::get().itemByHash(model->panelPosition(), hash);

	if (item.isValid() && item.isFile())
	{
		const QString itemName = lineEditor->text();
		const auto dot = static_cast<int>(itemName.lastIndexOf('.'));
		if (dot != -1)
		{
			QTimer::singleShot(0, Qt::CoarseTimer, lineEditor, [=]() {
				lineEditor->setSelection(0, dot);
			});
		}
	}
}

bool CFileListItemDelegate::eventFilter(QObject * object, QEvent * event)
{
	QWidget *editor = qobject_cast<QWidget*>(object);
	if (!editor)
		return QStyledItemDelegate::eventFilter(object, event);

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
		case Qt::Key_Enter: // Numpad Enter
		case Qt::Key_Return:
			// don't filter enter key events for QTextEdit
			if (!qobject_cast<QTextEdit *>(editor) && !qobject_cast<QPlainTextEdit*>(editor))
			{
				// commit data
				emit commitData(editor);
				emit closeEditor(editor);
				return true;
			}

			return QStyledItemDelegate::eventFilter(object, event);
		case Qt::Key_Escape:
			// don't commit data
			emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
			return true;
		case Qt::Key_Up:
		case Qt::Key_Down:
			// don't commit data
			emit closeEditor(editor, QAbstractItemDelegate::RevertModelCache);
			// Don't consume the event so that the TreeView will handle this event and highlight the next item properly
			return QStyledItemDelegate::eventFilter(object, event);
		default:
			return QStyledItemDelegate::eventFilter(object, event);
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
					return QStyledItemDelegate::eventFilter(object, event);

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

	return QStyledItemDelegate::eventFilter(object, event);
}

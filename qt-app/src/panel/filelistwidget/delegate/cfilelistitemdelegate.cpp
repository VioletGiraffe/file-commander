#include "cfilelistitemdelegate.h"
#include <assert.h>

CFileListItemDelegate::CFileListItemDelegate(QObject *parent) :
	QStyledItemDelegate(parent)
{
}

void CFileListItemDelegate::setEditorData(QWidget * editor, const QModelIndex & index) const
{
	QStyledItemDelegate::setEditorData(editor, index);
	QLineEdit * lineEditor = dynamic_cast<QLineEdit*>(editor);
	assert(lineEditor);
	const QString itemName = lineEditor->text();
	const int dot = itemName.indexOf('.');
	if (dot != -1)
	{
		// TODO: replace this with the new QTimer::singleShot lambda syntax once it's available (presumably Qt 5.4)
		QTimer* timer = new QTimer();
		timer->setSingleShot(true);
		connect(timer, &QTimer::timeout, [=](){
			lineEditor->setSelection(0, dot);
			timer->deleteLater();
		});
		timer->start(0);
	}
}

#include "cfilelistitemdelegate.h"
#include "ccontroller.h"

CFileListItemDelegate::CFileListItemDelegate(QObject *parent) :
	QStyledItemDelegate(parent)
{
}

QWidget *CFileListItemDelegate::createEditor(QWidget * parent, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
	QLineEdit * editor = dynamic_cast<QLineEdit*>(QStyledItemDelegate::createEditor(parent, option, index));
	connect(editor, &QLineEdit::returnPressed, [=](){
		//QStyledItemDelegate::setModelData(editor, const_cast<QAbstractItemModel*>(index.model()), index);
		//QMessageBox::warning(0, "Failed to rename a file", "Failed to rename");
		validateHeap();
	});
	return editor;
}

void CFileListItemDelegate::setModelData(QWidget * /*editor*/, QAbstractItemModel * /*model*/, const QModelIndex & /*index*/) const
{
	// Doing nothing since we only want to save changes when Enter is pressed
}

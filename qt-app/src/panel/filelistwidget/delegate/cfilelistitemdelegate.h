#ifndef CFILELISTITEMDELEGATE_H
#define CFILELISTITEMDELEGATE_H

#include "QtAppIncludes"

class CFileListItemDelegate : public QStyledItemDelegate
{
	Q_OBJECT
public:
	explicit CFileListItemDelegate(QObject *parent = 0);

	QWidget * createEditor(QWidget * parent, const QStyleOptionViewItem & option, const QModelIndex & index) const override;
	void setModelData(QWidget * editor, QAbstractItemModel * model, const QModelIndex & index) const override;

};

#endif // CFILELISTITEMDELEGATE_H

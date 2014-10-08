#ifndef CFILELISTITEMDELEGATE_H
#define CFILELISTITEMDELEGATE_H

#include "QtAppIncludes"

class CFileListItemDelegate : public QStyledItemDelegate
{
	Q_OBJECT
public:
	explicit CFileListItemDelegate(QObject *parent = 0);

	void setEditorData(QWidget * editor, const QModelIndex & index) const override;

	bool eventFilter(QObject *object, QEvent *event) override;
};

#endif // CFILELISTITEMDELEGATE_H

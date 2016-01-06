#ifndef CFILELISTITEMDELEGATE_H
#define CFILELISTITEMDELEGATE_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStyledItemDelegate>
RESTORE_COMPILER_WARNINGS

class CFileListItemDelegate : public QStyledItemDelegate
{
public:
	explicit CFileListItemDelegate(QObject *parent = 0);

	void setEditorData(QWidget * editor, const QModelIndex & index) const override;

	bool eventFilter(QObject *object, QEvent *event) override;
};

#endif // CFILELISTITEMDELEGATE_H

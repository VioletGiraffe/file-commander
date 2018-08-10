#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStyledItemDelegate>
RESTORE_COMPILER_WARNINGS

class CFileListItemDelegate : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate; // "Inherited" constructor

	void setEditorData(QWidget * editor, const QModelIndex & index) const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
};

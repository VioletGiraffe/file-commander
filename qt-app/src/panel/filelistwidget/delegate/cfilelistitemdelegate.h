#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QStyledItemDelegate>
RESTORE_COMPILER_WARNINGS

class CFileListItemDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	// Does nothing: the view calls it on every dataChanged over the edited cell, and refilling would discard the text typed so far
	// createEditor() fills the editor instead: a row's name never changes
	void setEditorData(QWidget * editor, const QModelIndex & index) const override;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
};

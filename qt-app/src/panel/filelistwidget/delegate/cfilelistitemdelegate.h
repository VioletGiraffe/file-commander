#pragma once

#include "panel/filelistwidget/filelistlook.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QStyledItemDelegate>
RESTORE_COMPILER_WARNINGS

// Paints the cells only. The view paints the selection and the cursor behind the row, in the fill rect defined here.
class CFileListItemDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	// Changes the row height: the view must lay its items out again
	void setLook(const FileListLook& look);
	[[nodiscard]] const FileListLook& look() const;
	// The part of a row that a selection or cursor fill covers
	[[nodiscard]] QRect rowFillRect(const QRect& row) const;

	QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	// Does nothing: the view calls it on every dataChanged over the edited cell, and refilling would discard the text typed so far
	// createEditor() fills the editor instead: a row's name never changes
	void setEditorData(QWidget * editor, const QModelIndex & index) const override;
	void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	FileListLook _look;
};

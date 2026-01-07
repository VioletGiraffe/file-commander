#pragma once
#include <QStyledItemDelegate>

class CFileListItemDelegate final : public QStyledItemDelegate
{
public:
	using QStyledItemDelegate::QStyledItemDelegate;

	void setEditorData(QWidget * editor, const QModelIndex & index) const override;

	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
};

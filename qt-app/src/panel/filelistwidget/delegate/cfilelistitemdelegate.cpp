#include "cfilelistitemdelegate.h"

#include "panel/columns.h"
#include "panel/filelistwidget/model/cfilelistmodel.h"


// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTreeView>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

// A row's fill is inset from the view's edges and stops short of the next row
constexpr int rowFillInset = 6, rowFillGap = 1;
// Between the fill's edges and the row's content
constexpr int contentHMargin = 6, contentVMargin = 4;

// The part of a cell its content occupies
[[nodiscard]] QRect cellContentRect(const QRect& cell, const QHeaderView& header, const int logicalColumn)
{
	QRect content = cell.adjusted(0, 0, 0, -rowFillGap);

	const int visualColumn = header.visualIndex(logicalColumn);
	if (visualColumn == 0)
		content.setLeft(content.left() + rowFillInset + contentHMargin);
	if (visualColumn == header.count() - 1)
		content.setRight(content.right() - rowFillInset - contentHMargin);

	return content;
}

} // namespace

QRect CFileListItemDelegate::rowFillRect(const QRect& row)
{
	return row.adjusted(rowFillInset, 0, -rowFillInset, -rowFillGap);
}

// Item rename handling
QWidget* CFileListItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
	QStyledItemDelegate::setEditorData(editor, index);
	auto* lineEditor = dynamic_cast<QLineEdit*>(editor);
	assert_and_return_r(lineEditor && index.isValid(), editor);

	auto* model = dynamic_cast<const CFileListModel*>(index.model());
	assert_and_return_message_r(model, "Something has changed in the model hierarchy", editor);

	if (model->rowAt(index).type() == File)
	{
		const QString itemName = lineEditor->text();
		const auto dot = static_cast<int>(itemName.lastIndexOf('.'));
		if (dot != -1)
		{
			QMetaObject::invokeMethod(lineEditor, [=]() {
				lineEditor->setSelection(0, dot);
			}, Qt::QueuedConnection);
		}
	}

	return editor;
}

void CFileListItemDelegate::setEditorData(QWidget* /*editor*/, const QModelIndex& /*index*/) const
{
}

void CFileListItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const auto* view = qobject_cast<const QTreeView*>(option.widget);
	assert_and_return_r(view, );

	QStyleOptionViewItem opt = option;
	opt.rect = cellContentRect(option.rect, *view->header(), index.column());
	QStyledItemDelegate::updateEditorGeometry(editor, opt, index);
}

void CFileListItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	const auto* view = qobject_cast<const QTreeView*>(option.widget);
	assert_and_return_r(view, );

	const bool selected = option.state.testFlag(QStyle::State_Selected);
	// A drag image is rendered by the delegate alone, into a pixmap: no view paints the selection behind the row
	if (selected && painter->device() != view->viewport())
		painter->fillRect(option.rect, option.palette.color(QPalette::Highlight));

	QStyleOptionViewItem opt = option;
	// On screen, the view has painted the selection and the cursor behind the row
	opt.state &= ~(QStyle::State_MouseOver | QStyle::State_Selected | QStyle::State_HasFocus);
	opt.rect = cellContentRect(option.rect, *view->header(), index.column());
	if (selected)
		opt.palette.setColor(QPalette::Text, option.palette.color(QPalette::HighlightedText));
	else if (index.column() != NameColumn)
		opt.palette.setColor(QPalette::Text, option.palette.color(QPalette::PlaceholderText));
	QStyledItemDelegate::paint(painter, opt, index);

	if (!_columnSeparatorsShown)
		return;

	// The last column ends at the view's edge
	if (view->header()->visualIndex(index.column()) == view->header()->count() - 1)
		return;

	// The column separator: a faint dashed line along the cell's right edge
	static constexpr int dashLength = 5, dashPeriod = dashLength * 2;
	QColor lineColor = option.palette.color(QPalette::Text);
	lineColor.setAlphaF(0.15f);
	const QRect& cell = option.rect;
	// The dashes start at multiples of dashPeriod in view coordinates, so they run on unbroken across rows
	const int phase = (cell.top() % dashPeriod + dashPeriod) % dashPeriod;
	for (int dashTop = cell.top() - phase; dashTop <= cell.bottom(); dashTop += dashPeriod)
	{
		const int top = std::max(dashTop, cell.top()), bottom = std::min(dashTop + dashLength - 1, cell.bottom());
		if (top <= bottom)
			painter->fillRect(QRect{ cell.right(), top, 1, bottom - top + 1 }, lineColor);
	}
}

QSize CFileListItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
	QSize hint = QStyledItemDelegate::sizeHint(option, index);
	hint.setHeight(std::max(hint.height(), option.fontMetrics.height() + 2 * contentVMargin) + rowFillGap);
	return hint;
}

void CFileListItemDelegate::setColumnSeparatorsShown(const bool shown)
{
	_columnSeparatorsShown = shown;
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

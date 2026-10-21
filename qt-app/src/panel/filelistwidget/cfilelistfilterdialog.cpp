#include "cfilelistfilterdialog.h"

// Submodule includes
#include "assert/advanced_assert.h"


DISABLE_COMPILER_WARNINGS
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShortcut>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QLineEdit(parent)
{
	_escShortcut = new QShortcut(QKeySequence("Esc"), parent, this, [this] {
			setVisible(false);
			emit filterTextConfirmed(QString{});
		}
	, Qt::WidgetWithChildrenShortcut);

	setVisible(false);

	assert_r(connect(this, &QLineEdit::textEdited, this, &CFileListFilterDialog::filterTextEdited));
}

void CFileListFilterDialog::showForEditing()
{
	setVisible(true);
	setFocus();
	selectAll();
}

void CFileListFilterDialog::setVisible(bool visible)
{
	// Hiding the focus widget would move the focus to the next one in the tab chain
	if (!visible && hasFocus())
		parentWidget()->setFocus();

	QLineEdit::setVisible(visible);
	_escShortcut->setEnabled(visible);
	if (visible)
	{
		// Positioned after showing: the first show resizes the box to its size hint
		move(0, parentWidget()->height() - height());
		raise();
	}
}

void CFileListFilterDialog::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
	{
		emit filterTextConfirmed(text());
		// consume the event
		event->accept();
		return;
	}

	QLineEdit::keyPressEvent(event);
}

bool CFileListFilterDialog::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::Resize)
	{
		const auto newSize = static_cast<QResizeEvent*>(event)->size();
		move(0, newSize.height() - height());
	}

	return QLineEdit::eventFilter(watched, event);
}

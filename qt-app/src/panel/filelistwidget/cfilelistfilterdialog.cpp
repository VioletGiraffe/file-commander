#include "cfilelistfilterdialog.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilelistfilterdialog.h"

#include <QKeyEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QShortcut>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QLineEdit(parent)
{
	_escShortcut = new QShortcut(QKeySequence("Esc"), parent, this, [this] {
			_escShortcut->setEnabled(false);
			setVisible(false);
			emit filterTextConfirmed(QString{});
		}
	, Qt::WidgetWithChildrenShortcut);

	_escShortcut->setEnabled(false);
	setVisible(false);

	assert_r(connect(this, &QLineEdit::textEdited, this, &CFileListFilterDialog::filterTextEdited));
}

void CFileListFilterDialog::showAt(const QPoint & bottomLeft)
{
	setVisible(true);
	move(bottomLeft.x(), bottomLeft.y() - height());

	_escShortcut->setEnabled(true);
	raise();
	setFocus();
	selectAll();
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

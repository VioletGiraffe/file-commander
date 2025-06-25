#include "cfilelistfilterdialog.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilelistfilterdialog.h"

#include <QKeyEvent>
#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QDialog(parent, Qt::CustomizeWindowHint | Qt::NoDropShadowWindowHint | Qt::FramelessWindowHint),
	ui(new Ui::CFileListFilterDialog)
{
	ui->setupUi(this);

	_escShortcut = new QShortcut(QKeySequence("Esc"), parent, this, &CFileListFilterDialog::close, Qt::WidgetWithChildrenShortcut);
	_escShortcut->setEnabled(false);

	ui->_lineEdit->installEventFilter(this);
}

CFileListFilterDialog::~CFileListFilterDialog()
{
	delete ui;
}

void CFileListFilterDialog::showAt(const QPoint & bottomLeft)
{
	setGeometry(QRect(parentWidget()->mapToGlobal(QPoint(bottomLeft.x(), bottomLeft.y()-height())), size()));
	show();

	_escShortcut->setEnabled(true);

	QTimer::singleShot(0, ui->_lineEdit, [this] {
		ui->_lineEdit->setFocus();
		ui->_lineEdit->selectAll();
	});

	QTimer::singleShot(15, this, [this] {
		emit filterTextChanged(ui->_lineEdit->text());
	});
}

void CFileListFilterDialog::hideEvent(QHideEvent* e)
{
	_escShortcut->setEnabled(false);

	emit filterTextChanged(QString{});
	QDialog::hideEvent(e);
}

bool CFileListFilterDialog::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::KeyPress)
	{
		QKeyEvent* ke = static_cast<QKeyEvent*>(event);
		if (ke->key() == Qt::Key_Return)
		{
			emit filterTextChanged(ui->_lineEdit->text());
		}
	}

	return QDialog::eventFilter(watched, event);
}

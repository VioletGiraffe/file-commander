#include "cfilelistfilterdialog.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilelistfilterdialog.h"

#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QDialog(parent, Qt::CustomizeWindowHint | Qt::NoDropShadowWindowHint | Qt::FramelessWindowHint),
	ui(new Ui::CFileListFilterDialog)
{
	ui->setupUi(this);

	connect(ui->_lineEdit, &QLineEdit::textEdited, this, &CFileListFilterDialog::filterTextChanged);

	_escShortcut = new QShortcut(QKeySequence("Esc"), parent, this, &CFileListFilterDialog::close, Qt::WidgetWithChildrenShortcut);
	_escShortcut->setEnabled(false);
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

	QTimer::singleShot(0, ui->_lineEdit, (void (QLineEdit::*)())&QLineEdit::setFocus);
	QTimer::singleShot(0, ui->_lineEdit, &QLineEdit::selectAll);
	emit filterTextChanged(ui->_lineEdit->text());
}

void CFileListFilterDialog::closeEvent(QCloseEvent * e)
{
	_escShortcut->setEnabled(false);

	emit filterTextChanged(QString());
	QDialog::closeEvent(e);
}

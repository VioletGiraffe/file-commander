#include "cfilelistfilterdialog.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include "ui_cfilelistfilterdialog.h"

#include <QKeyEvent>
#include <QShortcut>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QDialog(parent, Qt::CustomizeWindowHint | Qt::NoDropShadowWindowHint | Qt::FramelessWindowHint),
	ui(new Ui::CFileListFilterDialog)
{
	ui->setupUi(this);

	_escShortcut = new QShortcut(QKeySequence("Esc"), parent, this, &CFileListFilterDialog::close, Qt::WidgetWithChildrenShortcut);
	_escShortcut->setEnabled(false);

	assert_r(connect(ui->_lineEdit, &QLineEdit::textEdited, this, &CFileListFilterDialog::filterTextEdited));
	assert_r(connect(ui->_lineEdit, &QLineEdit::returnPressed, this, [this]() {
		emit filterTextConfirmed(ui->_lineEdit->text());
	}));
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
	activateWindow();
	ui->_lineEdit->selectAll();
	ui->_lineEdit->setFocus();
}

QString CFileListFilterDialog::filterText() const
{
	return ui->_lineEdit->text();
}

void CFileListFilterDialog::hideEvent(QHideEvent* e)
{
	_escShortcut->setEnabled(false);

	emit filterTextConfirmed(QString{});
	QDialog::hideEvent(e);
}

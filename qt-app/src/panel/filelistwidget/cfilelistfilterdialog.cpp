#include "cfilelistfilterdialog.h"
#include "ui_cfilelistfilterdialog.h"

DISABLE_COMPILER_WARNINGS
#include <QShortcut>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QDialog(parent, Qt::Popup),
	ui(new Ui::CFileListFilterDialog)
{
	ui->setupUi(this);

	connect(ui->_lineEdit, &QLineEdit::textEdited, this, &CFileListFilterDialog::filterTextChanged);

	new QShortcut(QKeySequence("Esc"), this, SLOT(close()), SLOT(close()), Qt::WidgetWithChildrenShortcut);
}

CFileListFilterDialog::~CFileListFilterDialog()
{
	delete ui;
}

void CFileListFilterDialog::showAt(const QPoint & bottomLeft)
{
	setGeometry(QRect(parentWidget()->mapToGlobal(QPoint(bottomLeft.x(), bottomLeft.y()-height())), size()));
	show();
	QTimer::singleShot(0, [this](){
		// TODO: why doesn't it compile as QTimer::singleShot(0, ui->_lineEdit, &QLineEdit::setFocus)
		ui->_lineEdit->setFocus();
	});
	QTimer::singleShot(0, ui->_lineEdit, &QLineEdit::selectAll);
	emit filterTextChanged(ui->_lineEdit->text());
}

void CFileListFilterDialog::closeEvent(QCloseEvent * e)
{
	emit filterTextChanged(QString());
	QDialog::closeEvent(e);
}

#include "cfilelistfilterdialog.h"
#include "ui_cfilelistfilterdialog.h"

CFileListFilterDialog::CFileListFilterDialog(QWidget *parent) :
	QDialog(parent, Qt::Popup),
	ui(new Ui::CFileListFilterDialog)
{
	ui->setupUi(this);

	connect(ui->_lineEdit, SIGNAL(textEdited(QString)), SIGNAL(filterTextChanged(QString)));

	new QShortcut(QKeySequence("Esc"), this, SLOT(close()), SLOT(close()), Qt::WidgetWithChildrenShortcut);

//	adjustSize();
}

CFileListFilterDialog::~CFileListFilterDialog()
{
	delete ui;
}

void CFileListFilterDialog::showAt(const QPoint & bottomLeft)
{
	setGeometry(QRect(parentWidget()->mapToGlobal(QPoint(bottomLeft.x(), bottomLeft.y()-height())), size()));
	show();
	QTimer::singleShot(0, ui->_lineEdit, SLOT(setFocus()));
	QTimer::singleShot(0, ui->_lineEdit, SLOT(selectAll()));
	emit filterTextChanged(ui->_lineEdit->text());
}

void CFileListFilterDialog::closeEvent(QCloseEvent * e)
{
	emit filterTextChanged(QString());
	QDialog::closeEvent(e);
}

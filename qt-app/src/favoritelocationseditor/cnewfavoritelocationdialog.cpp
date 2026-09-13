#include "cnewfavoritelocationdialog.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include "ui_cnewfavoritelocationdialog.h"
RESTORE_COMPILER_WARNINGS

CNewFavoriteLocationDialog::CNewFavoriteLocationDialog(QWidget *parent, bool subcategory) :
	QDialog(parent),
	ui(new Ui::CNewFavoriteLocationDialog)
{
	ui->setupUi(this);
	if (subcategory)
	{
		ui->_locationEditor->setVisible(false);
		ui->_locationLabel->setVisible(false);
	}
}

CNewFavoriteLocationDialog::~CNewFavoriteLocationDialog()
{
	delete ui;
}

QString CNewFavoriteLocationDialog::name() const
{
	return ui->_nameEditor->text();
}

QString CNewFavoriteLocationDialog::location() const
{
	return ui->_locationEditor->text();
}

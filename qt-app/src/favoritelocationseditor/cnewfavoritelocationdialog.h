#ifndef CNEWFAVORITELOCATIONDIALOG_H
#define CNEWFAVORITELOCATIONDIALOG_H

#include "utils/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CNewFavoriteLocationDialog;
}

class CNewFavoriteLocationDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CNewFavoriteLocationDialog(QWidget *parent, bool subcategory);
	~CNewFavoriteLocationDialog();

	QString name() const;
	QString location() const;

private:
	Ui::CNewFavoriteLocationDialog *ui;
};

#endif // CNEWFAVORITELOCATIONDIALOG_H

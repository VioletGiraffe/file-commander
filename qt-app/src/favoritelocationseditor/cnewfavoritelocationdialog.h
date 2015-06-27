#ifndef CNEWFAVORITELOCATIONDIALOG_H
#define CNEWFAVORITELOCATIONDIALOG_H

#include <QDialog>

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

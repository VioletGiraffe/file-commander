#ifndef CFILELISTFILTERDIALOG_H
#define CFILELISTFILTERDIALOG_H

#include "../../QtAppIncludes"

namespace Ui {
class CFileListFilterDialog;
}

class CFileListFilterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CFileListFilterDialog(QWidget *parent = 0);
	~CFileListFilterDialog();

	void showAt(const QPoint& bottomLeft);

signals:
	void filterTextChanged(QString text);

private:
	Ui::CFileListFilterDialog *ui;
};

#endif // CFILELISTFILTERDIALOG_H

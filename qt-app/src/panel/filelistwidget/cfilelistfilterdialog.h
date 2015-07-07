#ifndef CFILELISTFILTERDIALOG_H
#define CFILELISTFILTERDIALOG_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

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

protected:
	void closeEvent(QCloseEvent* e) override;

private:
	Ui::CFileListFilterDialog *ui;
};

#endif // CFILELISTFILTERDIALOG_H

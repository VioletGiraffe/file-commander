#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFileListFilterDialog;
}

class QShortcut;

class CFileListFilterDialog final : public QDialog
{
	Q_OBJECT
public:
	explicit CFileListFilterDialog(QWidget *parent);
	~CFileListFilterDialog() override;

	void showAt(const QPoint& bottomLeft);

signals:
	void filterTextChanged(QString text);

protected:
	void closeEvent(QCloseEvent* e) override;

private:
	Ui::CFileListFilterDialog *ui;
	QShortcut* _escShortcut;
};

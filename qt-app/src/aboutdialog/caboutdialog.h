#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CAboutDialog;
}

class CAboutDialog : public QDialog
{
public:
	explicit CAboutDialog(QWidget *parent);
	~CAboutDialog() override;

private:
	Ui::CAboutDialog *ui;
};

#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

namespace Ui {
class CAboutDialog;
}

class CAboutDialog : public QDialog
{
public:
	explicit CAboutDialog(QWidget *parent, const std::vector<QString>& activePluginNames);
	~CAboutDialog() override;

private:
	Ui::CAboutDialog *ui;
};

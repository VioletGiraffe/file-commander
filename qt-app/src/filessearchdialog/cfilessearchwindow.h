#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFilesSearchWindow;
}

class CFilesSearchWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit CFilesSearchWindow(QWidget *parent = 0);
	~CFilesSearchWindow();

private:
	Ui::CFilesSearchWindow *ui;
};


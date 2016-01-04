#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QDialog>
RESTORE_COMPILER_WARNINGS

namespace Ui {
	class CUpdaterDialog;
}

class CAutoUpdaterGithub;

class CUpdaterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit CUpdaterDialog(QWidget *parent, CAutoUpdaterGithub& updater);
	~CUpdaterDialog();

	void downloadProgress(int progress);

private:
	Ui::CUpdaterDialog *ui;

	CAutoUpdaterGithub& _updater;
};


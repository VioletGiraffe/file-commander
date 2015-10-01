#pragma once

#include "compiler/compiler_warnings_control.h"
#include "threading/cinterruptablethread.h"
#include "threading/cexecutionqueue.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <thread>

namespace Ui {
class CFilesSearchWindow;
}

class CFilesSearchWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit CFilesSearchWindow(QWidget *parent, const QString& root);
	~CFilesSearchWindow();

private:
	void search();

	void updateUi();

private:
	Ui::CFilesSearchWindow *ui;
	QTimer _uiUpdateTimer;

	CInterruptableThread _workerThread;
	CExecutionQueue _uiQueue;
};


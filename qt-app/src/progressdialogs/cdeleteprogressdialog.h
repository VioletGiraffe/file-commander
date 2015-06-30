#ifndef CDELETEPROGRESSDIALOG_H
#define CDELETEPROGRESSDIALOG_H

#include "fileoperations/coperationperformer.h"

DISABLE_COMPILER_WARNINGS
#include <QTimer>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CDeleteProgressDialog;
}

class CMainWindow;
class COperationPerformer;

class CDeleteProgressDialog : public QWidget, protected CFileOperationObserver
{
	Q_OBJECT

public:
	explicit CDeleteProgressDialog(std::vector<CFileSystemObject> source, QString destination, CMainWindow * mainWindow);
	~CDeleteProgressDialog();

// Callbacks
	void onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* B/s*/) override;
	void onProcessHalted(HaltReason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage) override; // User decision required (file exists, file is read-only etc.)
	void onProcessFinished(QString message = QString()) override; // Done or canceled
	void onCurrentFileChanged(QString file) override; // Starting to process a new file

protected:
	virtual void closeEvent(QCloseEvent * e);

private slots:
	void cancelPressed();
	void pauseResume();
	void background();

// Utility slots
	void setMinSize();
	void processEvents();

private:
	void cancel();

private:
	Ui::CDeleteProgressDialog *ui;
	COperationPerformer * _performer;
	CMainWindow         * _mainWindow;
	QTimer                _eventsProcessTimer;
};

#endif // CDELETEPROGRESSDIALOG_H

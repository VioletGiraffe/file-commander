#ifndef CDELETEPROGRESSDIALOG_H
#define CDELETEPROGRESSDIALOG_H

#include "fileoperations/coperationperformer.h"
#include "../QtAppIncludes"

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
	virtual void onProgressChanged(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int filePercentage, uint64_t speed /* B/s*/);
	virtual void onProcessHalted(HaltReason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage); // User decision required (file exists, file is read-only etc.)
	virtual void onProcessFinished(QString message = QString()); // Done or canceled
	virtual void onCurrentFileChanged(QString file); // Starting to process a new file

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

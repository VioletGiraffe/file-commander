#ifndef CCOPYMOVEDIALOG_H
#define CCOPYMOVEDIALOG_H

#include "fileoperations/coperationperformer.h"

#include "../QtAppIncludes"

namespace Ui {
class CCopyMoveDialog;
}

class CMainWindow;

class CCopyMoveDialog : public QWidget, protected CFileOperationObserver
{
	Q_OBJECT

public:
	explicit CCopyMoveDialog(Operation, std::vector<CFileSystemObject> source, QString destination, CMainWindow * mainWindow);
	~CCopyMoveDialog();

// Callbacks
	virtual void onProgressChanged(int totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, int filePercentage, uint64_t speed /* B/s*/);
	virtual void onProcessHalted(HaltReason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage); // User decision required (file exists, file is read-only etc.)
	virtual void onProcessFinished(QString message = QString()); // Done or canceled
	virtual void onCurrentFileChanged(QString file); // Starting to process a new file

signals:
	void closed();

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
	Ui::CCopyMoveDialog * ui;
	COperationPerformer * _performer;
	CMainWindow         * _mainWindow;
	Operation             _op;
	QTimer                _eventsProcessTimer;
	const QString         _titleTemplate;
	const QString         _labelTemplate;
	uint64_t              _speed;
};

#endif // CCOPYMOVEDIALOG_H

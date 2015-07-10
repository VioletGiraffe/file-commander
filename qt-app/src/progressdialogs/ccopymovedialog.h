#ifndef CCOPYMOVEDIALOG_H
#define CCOPYMOVEDIALOG_H

#include "fileoperations/coperationperformer.h"

DISABLE_COMPILER_WARNINGS
#include <QTimer>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

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
	void onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* B/s*/) override;
	void onProcessHalted(HaltReason, CFileSystemObject source, CFileSystemObject dest, QString errorMessage) override; // User decision required (file exists, file is read-only etc.)
	void onProcessFinished(QString message = QString()) override; // Done or canceled
	void onCurrentFileChanged(QString file) override; // Starting to process a new file

signals:
	void closed();

protected:
	void closeEvent(QCloseEvent * e) override;

private slots:
	// True if cancelled, false if the user chose to continue
	bool cancelPressed();
	void pauseResume();
	void background();

// Utility slots
	void processEvents();

private:
	void setMinSize();
	void cancel();

private:
	Ui::CCopyMoveDialog * ui;
	std::unique_ptr<COperationPerformer> _performer;
	CMainWindow         * _mainWindow;
	Operation             _op;
	QTimer                _eventsProcessTimer;
	const QString         _titleTemplate;
	const QString         _labelTemplate;
	uint64_t              _speed;
};

#endif // CCOPYMOVEDIALOG_H

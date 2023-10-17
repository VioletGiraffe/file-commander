#pragma once

#include "fileoperations/coperationperformer.h"

DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <memory>

namespace Ui {
class CDeleteProgressDialog;
}

class CMainWindow;
class COperationPerformer;
class QTimer;

class CDeleteProgressDialog final : public QWidget, protected CFileOperationObserver
{
public:
	CDeleteProgressDialog(QWidget* parent, std::vector<CFileSystemObject>&& source, QString destination, CMainWindow * mainWindow);
	~CDeleteProgressDialog() override;

// Callbacks
	void onProgressChanged(float totalPercentage, size_t numFilesProcessed, size_t totalNumFiles, float filePercentage, uint64_t speed /* B/s*/, uint32_t secondsRemaining) override;
	void onProcessHalted(HaltReason, const CFileSystemObject& source, const CFileSystemObject& dest, const QString& errorMessage) override; // User decision required (file exists, file is read-only etc.)
	void onProcessFinished(const QString& message) override; // Done or canceled
	void onCurrentFileChanged(const QString& file) override; // Starting to process a new file

protected:
	void closeEvent(QCloseEvent * e) override;

private:
	void cancelPressed();
	void pauseResume();
	void background();

private:
	void cancel();

private:
	Ui::CDeleteProgressDialog *ui;
	const std::unique_ptr<COperationPerformer> _performer;
	CMainWindow         * _mainWindow;
	QTimer*               _eventsProcessTimer = nullptr;
};

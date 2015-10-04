#pragma once

#include "compiler/compiler_warnings_control.h"
#include "filesearchengine/cfilesearchengine.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFilesSearchWindow;
}

class QLabel;

class CFilesSearchWindow : public QMainWindow, public CFileSearchEngine::FileSearchListener
{
	Q_OBJECT

public:
	explicit CFilesSearchWindow(const QString& root);
	~CFilesSearchWindow();

	void itemScanned(const QString& currentItem) override;
	void matchFound(const QString& path) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed) override;

private:
	void search();

private:
	Ui::CFilesSearchWindow *ui;
	QLabel* _progressLabel;

	CFileSearchEngine& _engine;
};


#pragma once

#include "compiler/compiler_warnings_control.h"
#include "filesearchengine/cfilesearchengine.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
#include <QTimer>
RESTORE_COMPILER_WARNINGS

namespace Ui {
class CFilesSearchWindow;
}

class QLabel;

class CFilesSearchWindow final : public QMainWindow, public CFileSearchEngine::FileSearchListener
{
public:
	explicit CFilesSearchWindow(const std::vector<QString>& targets);
	~CFilesSearchWindow() override;

	void itemScanned(const QString& currentItem) override;
	void matchFound(const QString& path) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed) override;

private:
	void search();

	void addResultsToUi();

private:
	Ui::CFilesSearchWindow *ui;
	CFileSearchEngine& _engine;

	QLabel* _progressLabel;
	QTimer _resultsListUpdateTimer;
	std::vector<QString> _matches;
};


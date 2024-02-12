#pragma once

#include "compiler/compiler_warnings_control.h"
#include "filesearchengine/cfilesearchengine.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <vector>

namespace Ui {
class CFilesSearchWindow;
}

class QLabel;

class CFilesSearchWindow final : public QMainWindow, public CFileSearchEngine::FileSearchListener
{
public:
	CFilesSearchWindow(const std::vector<QString>& targets, QWidget* parent);
	~CFilesSearchWindow() override;

	void itemScanned(const QString& currentItem) override;
	void matchFound(const QString& path) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint32_t speed) override;

private:
	void search();

	void addResultToUi(const QString& path);

	void saveResults();
	void loadResults();

private:
	Ui::CFilesSearchWindow *ui = nullptr;
	CFileSearchEngine& _engine;

	QLabel* _progressLabel = nullptr;
	std::vector<QString> _matches;
};


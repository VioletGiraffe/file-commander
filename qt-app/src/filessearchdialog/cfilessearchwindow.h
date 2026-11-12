#pragma once

#include "cfilesystemobject.h"
#include "filesearchengine/cfilesearchengine.h"
#include "filesearchengine/cfilesearchresultsbuffer.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <vector>

namespace Ui {
class CFilesSearchWindow;
}

class QAction;
class QLabel;

class CIconProvider;

class CFilesSearchWindow final : public QMainWindow
{
public:
	CFilesSearchWindow(const QString& initialSearchInText, CIconProvider& iconProvider, QWidget* parent);
	~CFilesSearchWindow() override;

private:
	void search();

	void displayPendingResults();
	void showSearchCompletion(const CFileSearchResultsBuffer::Completion& completion);

	void setProgressText(const QString& text, Qt::TextElideMode elideMode);
	// The caller disables the list's updates around a batch of these
	void addResultToUi(const CFileSystemObject& object, bool reachedThroughLink);

	void saveResults();
	void loadResults();
	// Stops a running search first
	void sendResultsToPanel();

	void showContextMenu(const QPoint &pos);
	void copyCurrentResultPath() const;

private:
	CIconProvider& _iconProvider;
	// What the search has reported and this window has not shown yet. Declared before the engine, whose threads report into it.
	CFileSearchResultsBuffer _pendingResults;
	CFileSearchEngine _engine;
	std::vector<QString> _matches;
	// Set from a started search until its completion is shown: the engine's own flag clears before its results are all shown
	bool _searchActive = false;

	QAction* _copyPathAction = nullptr;

	Ui::CFilesSearchWindow *ui = nullptr;
};


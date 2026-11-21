#pragma once

#include "filesearchengine/cfilesearchengine.h"
#include "filesearchengine/cfilesearchresultsbuffer.h"
#include "iconprovider/ciconprovider.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <vector>

namespace Ui {
class CFilesSearchWindow;
}

class QAction;
class QTimer;

class CFileListModel;

class CFilesSearchWindow final : public QMainWindow, private IconsChangedListener
{
public:
	CFilesSearchWindow(const QString& initialSearchInText, CIconProvider& iconProvider, QWidget* parent);
	~CFilesSearchWindow() override;

private:
	void search();

	void displayPendingResults();
	void showSearchCompletion(const CFileSearchResultsBuffer::Completion& completion);
	[[nodiscard]] bool currentResultIsOnScreen() const;
	// Installed on the application while a search runs: records input to this window
	bool eventFilter(QObject* watched, QEvent* event) override;
	void showEvent(QShowEvent* event) override;

	void setProgressText(const QString& text, Qt::TextElideMode elideMode);

	void saveResults();
	void loadResults();
	// Stops a running search first
	void sendResultsToPanel();
	// In the displayed order
	[[nodiscard]] QStringList resultPaths() const;

	void showContextMenu(const QPoint &pos);
	void copyCurrentResultPath() const;

	void onIconsChanged(const std::vector<qulonglong>& objectHashes) override;
	void onAllIconsChanged() override;

private:
	CIconProvider& _iconProvider;
	CFileListModel* _results = nullptr;
	// What the search has reported and this window has not shown yet. Declared before the engine, whose threads report into it.
	CFileSearchResultsBuffer _pendingResults;
	CFileSearchEngine _engine;
	// Runs while a search does: each timeout shows what it reported since the previous one
	QTimer* _resultsTimer = nullptr;
	// Set from a started search until its completion is shown: the engine's own flag clears before its results are all shown
	bool _searchActive = false;
	// The end of a search leaves the focus and the current row alone after input during it
	bool _userActedDuringSearch = false;

	QAction* _copyPathAction = nullptr;

	Ui::CFilesSearchWindow *ui = nullptr;
};


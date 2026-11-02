#pragma once

#include "cfilesystemobject.h"
#include "filesearchengine/cfilesearchengine.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <deque>
#include <mutex>
#include <optional>
#include <stdint.h>
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
	void matchFound(const CFileSystemObject& item, bool reachedThroughLink) override;
	void searchFinished(CFileSearchEngine::SearchStatus status, uint64_t itemsScanned, uint64_t inconclusiveItems, uint64_t msElapsed) override;

private:
	struct Match {
		CFileSystemObject item;
		bool reachedThroughLink;
	};

	struct SearchCompletion {
		CFileSearchEngine::SearchStatus status;
		uint64_t itemsScanned;
		uint64_t inconclusiveItems;
		uint64_t msElapsed;
	};

	// What the search threads have reported and the UI thread has not shown yet
	struct PendingResults {
		std::deque<Match> matches;
		QString lastScannedItem; // Empty once shown
		std::optional<SearchCompletion> completion; // Shown only after every match before it
		bool displayScheduled = false;
	};

	void search();

	// Requires _pendingResultsMutex. Posts at most one displayPendingResults() at a time, however many reports arrive.
	void scheduleDisplayLocked();
	void displayPendingResults();
	void showSearchCompletion(const SearchCompletion& completion);

	void setProgressText(const QString& text, Qt::TextElideMode elideMode);
	// The caller disables the list's updates around a batch of these
	void addResultToUi(const CFileSystemObject& object, bool reachedThroughLink);

	void saveResults();
	void loadResults();

	void showContextMenu(const QPoint &pos);

private:
	CFileSearchEngine _engine;
	std::vector<QString> _matches;
	// Set from a started search until its completion is shown: the engine's own flag clears before its results are all shown
	bool _searchActive = false;

	std::mutex _pendingResultsMutex;
	PendingResults _pendingResults; // Guarded by _pendingResultsMutex

	Ui::CFilesSearchWindow *ui = nullptr;
};


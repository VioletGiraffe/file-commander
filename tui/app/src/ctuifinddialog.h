#pragma once

#include "ctuifields.h"

#include "filesearchengine/cfilesearchengine.h"
#include "filesearchengine/cfilesearchresultsbuffer.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TDialog
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <chrono>
#include <stddef.h>
#include <variant>

class CTuiFindResultsList;
class CTuiTextView;
class TButton;
class TCheckBoxes;
class TInputLine;

// Finds files by name and contents, and lists them as they are found.
// The application keeps it between uses, so it shows the last search's results again.
class CTuiFindDialog final : public TDialog
{
public:
	explicit CTuiFindDialog(const TRect& bounds);

	struct GoToResult { QString path; };
	struct SendResultsToPanel { QStringList paths; };
	// What the dialog closed for: monostate when it closed without a choice
	using Outcome = std::variant<std::monostate, GoToResult, SendResultsToPanel>;

	// Runs the dialog over the whole desktop. A running search stops when the dialog closes.
	// searchInText: the "Search in" field's text; baseDir: the folder a relative path in it starts from.
	[[nodiscard]] Outcome exec(const QString& searchInText, const QString& baseDir);

	// Shows what the search reported since the last time, at most once per display interval
	void getEvent(TEvent& event) override;
	void handleEvent(TEvent& event) override;

private:
	void startSearch();
	void showPendingResults(size_t maxMatches);
	void showCompletion(const CFileSearchResultsBuffer::Completion& completion);
	void setSearchActive(bool active);
	void setStatus(const QString& text);
	// Under the list, elided only past the dialog's width: the rows show paths relative to the folder searched in
	void showCurrentResultPath();
	void goToCurrentResult();
	// Unless there are none
	void sendResultsToPanel();
	void saveOptions() const;

private:
	// Owned by this dialog
	TInputLine* _name;
	TCheckBoxes* _nameOptions;
	TInputLine* _contents;
	TCheckBoxes* _contentsOptions;
	TInputLine* _searchIn;
	TInputLine* _excludedFolders;
	TButton* _searchButton;
	CTuiFindResultsList* _results;
	CTuiTextView* _currentResultPath;
	CTuiTextView* _status;

	CTuiSavedFieldHistory _nameHistory, _contentsHistory, _searchInHistory;

	QString _baseDir;
	// A result's path is shown without it: the single folder searched in, ending with '/'. Empty when there is none.
	QString _searchRootPrefix;
	Outcome _outcome;

	// Before the engine, whose threads report into it
	CFileSearchResultsBuffer _pendingResults;
	CFileSearchEngine _engine;
	// Set from a started search until its completion is shown: the engine's own flag clears before its results are all shown
	bool _searchActive = false;
	std::chrono::steady_clock::time_point _nextResultsDisplayTime;
};

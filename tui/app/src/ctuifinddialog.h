#pragma once

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
#include <optional>
#include <stddef.h>

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

	// Runs the dialog over the whole desktop. A running search stops when the dialog closes.
	// searchInText: the "Search in" field's text; baseDir: the folder a relative path in it starts from.
	// Returns the path of the result chosen to go to; nullopt: closed without one.
	[[nodiscard]] std::optional<QString> exec(const QString& searchInText, const QString& baseDir);

	// Shows what the search reported since the last time, at most once per display interval
	void getEvent(TEvent& event) override;
	void handleEvent(TEvent& event) override;

private:
	void startSearch();
	void showPendingResults(size_t maxMatches);
	void showCompletion(const CFileSearchResultsBuffer::Completion& completion);
	void setSearchActive(bool active);
	void setStatus(const QString& text);
	void goToCurrentResult();
	void saveOptions() const;

private:
	// Owned by this dialog
	TInputLine* _name;
	TCheckBoxes* _nameOptions;
	TInputLine* _contents;
	TCheckBoxes* _contentsOptions;
	TInputLine* _searchIn;
	TButton* _searchButton;
	CTuiFindResultsList* _results;
	CTuiTextView* _status;

	// Most recent first
	QStringList _nameHistory, _contentsHistory, _searchInHistory;

	QString _baseDir;
	// A result's path is shown without it: the single folder searched in, ending with '/'. Empty when there is none.
	QString _searchRootPrefix;
	std::optional<QString> _chosenPath;

	// Before the engine, whose threads report into it
	CFileSearchResultsBuffer _pendingResults;
	CFileSearchEngine _engine;
	// Set from a started search until its completion is shown: the engine's own flag clears before its results are all shown
	bool _searchActive = false;
	std::chrono::steady_clock::time_point _nextResultsDisplayTime;
};

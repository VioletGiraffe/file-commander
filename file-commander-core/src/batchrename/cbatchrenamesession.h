#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/batchrename.h"
#include "threading/cbackgroundtask.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stddef.h>
#include <utility>
#include <vector>

// A batch rename dialog's model, free of UI: the items, the new names the rules give them, and the renaming and its undo
class CBatchRenameSession
{
public:
	enum class PreviewState
	{
		Unchanged,
		Renamable,
		InvalidName, // See Row::nameRejection
		NameTaken    // Another item gets the same new name
	};

	enum class Outcome
	{
		None,
		Renamed,
		Unchanged,
		Failed,   // See Row::failure
		Cancelled // The step stopped before this item
	};

	struct Row
	{
		CFileSystemObject item;
		CEntryPath path;
		QString newName;
		PreviewState state = PreviewState::Unchanged;
		NameRejection nameRejection = NameRejection::None;
		// The last step's result for the item, shown until the rules change
		Outcome outcome = Outcome::None;
		std::optional<InlineRenameResult> failure; // Set exactly when the outcome is Failed
	};

	enum class Tone { Normal, Dimmed, Error };
	struct Status
	{
		QString text;
		Tone tone = Tone::Normal;
	};

	enum class Step { Apply, Undo };

	// The values a dialog accepts for the counter's options
	struct Range
	{
		int min, max;
	};
	static constexpr Range counterStartRange{ 0, 999'999'999 }, counterStepRange{ 1, 1'000'000 }, counterDigitsRange{ 1, 10 };

	// Previews the default rules. A root, which cannot be renamed, is left out.
	explicit CBatchRenameSession(const std::vector<CFileSystemObject>& items);

	// The rules a dialog starts with: the defaults, with the find options and the counter as saved by saveOptions()
	[[nodiscard]] static BatchRenameRules initialRules();
	static void saveOptions(const BatchRenameRules& rules);

	// Previews the rules, and drops the outcomes shown
	void setRules(BatchRenameRules rules);

	// In the order given: the counter follows it
	[[nodiscard]] const std::vector<Row>& rows() const noexcept { return _rows; }
	// What the row shows: the last step's outcome, otherwise its preview state
	[[nodiscard]] static Status statusOf(const Row& row);
	// Empty unless the find text is an invalid regex
	[[nodiscard]] const QString& findPatternError() const noexcept { return _findPatternError; }

	// Apply: some item gets a new name, none an invalid or taken one, and no outcomes are shown. Undo: the last Apply renamed something.
	// Neither while a step is started and not finished.
	[[nodiscard]] bool canRun(Step step) const noexcept;
	// The current paths of the items the step renames
	[[nodiscard]] QStringList pathsRenamedBy(Step step) const;
	// Requires canRun(step). The items to rename with batchRename(), on any thread: the session is not used meanwhile.
	[[nodiscard]] std::vector<BatchRenameItem> startStep(Step step);
	// Requires a started step. results: batchRename()'s, for the items startStep() returned. The rows then show the outcomes.
	// Returns the old and new path of each item renamed, as CFileSystemObject::fullAbsolutePath(): see CController::itemsRenamed().
	std::vector<std::pair<QString, QString>> finishStep(const std::vector<std::optional<InlineRenameResult>>& results);

private:
	using RowRenames = std::vector<std::pair<size_t, QString>>; // Row index and new name

	struct StartedStep
	{
		Step step;
		RowRenames renames;
	};

	void updatePreview();
	[[nodiscard]] RowRenames renamesFor(Step step) const;

private:
	std::vector<Row> _rows;
	BatchRenameRules _rules;
	QString _findPatternError;
	RowRenames _undo;
	std::optional<StartedStep> _startedStep;
	bool _anyRenamable = false;
	bool _anyError = false;
	bool _showingOutcomes = false;
};

// batchRename() on a CBackgroundTask, which the constructor starts, for a UI that polls it
class CBatchRenameRun
{
public:
	explicit CBatchRenameRun(std::vector<BatchRenameItem> items);
	~CBatchRenameRun(); // Cancels, also a paused run

	// Of the items, by rename attempts made: approximate, since an item retried once another frees its new name counts twice
	[[nodiscard]] int percent() const noexcept;
	[[nodiscard]] bool finished() { return _task.finished(); }
	// The renaming stops before the next item until resume(); the item being renamed completes
	void pause();
	void resume();
	// Paused while confirm() runs, then resumed unless it returns true; cancelAndWait() is then the caller's
	[[nodiscard]] bool confirmCancelWhilePaused(const std::function<bool()>& confirm);
	// Returns once the item being renamed is done, also when paused; the rest stay unattempted
	void cancelAndWait();
	// Requires finished(). batchRename()'s results: an item never attempted is nullopt.
	[[nodiscard]] std::vector<std::optional<InlineRenameResult>> results() const;

private:
	using Results = std::vector<std::optional<InlineRenameResult>>;

	const size_t _itemCount;
	std::atomic<size_t> _attempts{ 0 };
	std::mutex _pauseMutex;
	std::condition_variable _resumed;
	bool _paused = false; // Guarded by _pauseMutex
	// Last: destroyed first, joining the renaming before the fields it writes are gone
	CBackgroundTask<Results> _task{ "Batch rename" };
};

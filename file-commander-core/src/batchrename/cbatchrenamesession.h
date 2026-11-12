#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/batchrename.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <functional>
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
	[[nodiscard]] bool canRun(Step step) const noexcept;
	// The current paths of the items the step renames
	[[nodiscard]] QStringList pathsRenamedBy(Step step) const;
	// Requires canRun(step). shouldContinue: see batchRename(). The rows then show the outcomes.
	void run(Step step, const std::function<bool()>& shouldContinue);

private:
	using RowRenames = std::vector<std::pair<size_t, QString>>; // Row index and new name

	void updatePreview();
	[[nodiscard]] RowRenames renamesFor(Step step) const;

private:
	std::vector<Row> _rows;
	BatchRenameRules _rules;
	QString _findPatternError;
	RowRenames _undo;
	bool _anyRenamable = false;
	bool _anyError = false;
	bool _showingOutcomes = false;
};

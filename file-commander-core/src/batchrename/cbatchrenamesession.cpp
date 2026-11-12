#include "cbatchrenamesession.h"

#include "settings.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "fileoperations/fileoperationtexts.h"


DISABLE_COMPILER_WARNINGS
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

[[nodiscard]] static BatchRenameSource sourceOf(const CFileSystemObject& item, const CEntryPath& path)
{
	const CEntryPath parent = path.parent();
	return {
		.name = item.name().toString(),
		.extension = item.extension().toString(),
		.parentFolderName = parent.isRoot() ? QString{} : parent.name(),
		.modificationTime = QDateTime::fromSecsSinceEpoch(item.modificationTime()),
	};
}

CBatchRenameSession::CBatchRenameSession(const std::vector<CFileSystemObject>& items)
{
	for (const CFileSystemObject& item : items)
	{
		// A real panel item always has a valid absolute path
		if (const auto path = parseOperationPath(item.fullAbsolutePath()); path && !path->isRoot())
			_rows.push_back({ .item = item, .path = *path });
	}

	updatePreview();
}

BatchRenameRules CBatchRenameSession::initialRules()
{
	BatchRenameRules rules;
	const QSettings s;
	rules.findIsRegex = s.value(KEY_BATCH_RENAME_FIND_IS_REGEX, rules.findIsRegex).toBool();
	rules.findIsCaseSensitive = s.value(KEY_BATCH_RENAME_FIND_IS_CASE_SENSITIVE, rules.findIsCaseSensitive).toBool();
	rules.counterStart = s.value(KEY_BATCH_RENAME_COUNTER_START, rules.counterStart).toInt();
	rules.counterStep = s.value(KEY_BATCH_RENAME_COUNTER_STEP, rules.counterStep).toInt();
	rules.counterDigits = s.value(KEY_BATCH_RENAME_COUNTER_DIGITS, rules.counterDigits).toInt();
	return rules;
}

void CBatchRenameSession::saveOptions(const BatchRenameRules& rules)
{
	QSettings s;
	s.setValue(KEY_BATCH_RENAME_FIND_IS_REGEX, rules.findIsRegex);
	s.setValue(KEY_BATCH_RENAME_FIND_IS_CASE_SENSITIVE, rules.findIsCaseSensitive);
	s.setValue(KEY_BATCH_RENAME_COUNTER_START, rules.counterStart);
	s.setValue(KEY_BATCH_RENAME_COUNTER_STEP, rules.counterStep);
	s.setValue(KEY_BATCH_RENAME_COUNTER_DIGITS, rules.counterDigits);
}

void CBatchRenameSession::setRules(BatchRenameRules rules)
{
	_rules = std::move(rules);
	for (Row& row : _rows)
	{
		row.outcome = Outcome::None;
		row.failure.reset();
	}
	_showingOutcomes = false;

	updatePreview();
}

CBatchRenameSession::Status CBatchRenameSession::statusOf(const Row& row)
{
	switch (row.outcome)
	{
	case Outcome::None:
		break;
	case Outcome::Renamed:
		return { QObject::tr("Renamed") };
	case Outcome::Unchanged:
		return { QObject::tr("Unchanged") };
	case Outcome::Failed:
		assert_debug_only(row.failure);
		return { QObject::tr("Failed: %1").arg(inlineRenameFailureText(*row.failure)), Tone::Error };
	case Outcome::Cancelled:
		return { QObject::tr("Cancelled") };
	}

	switch (row.state)
	{
	case PreviewState::Unchanged:
		return { QObject::tr("Unchanged"), Tone::Dimmed };
	case PreviewState::Renamable:
		return {};
	case PreviewState::InvalidName:
		return { newNameRejectionText(row.nameRejection), Tone::Error };
	case PreviewState::NameTaken:
		return { QObject::tr("Another item gets the same name"), Tone::Error };
	}

	assert_and_return_unconditional_r("Unknown batch rename preview state", {});
}

bool CBatchRenameSession::canRun(const Step step) const noexcept
{
	if (step == Step::Undo)
		return !_undo.empty();

	return _anyRenamable && !_anyError && !_showingOutcomes;
}

QStringList CBatchRenameSession::pathsRenamedBy(const Step step) const
{
	QStringList paths;
	for (const auto& [row, name] : renamesFor(step))
		paths.push_back(_rows[row].item.fullAbsolutePath());

	return paths;
}

std::vector<std::pair<QString, QString>> CBatchRenameSession::run(const Step step, const std::function<bool()>& shouldContinue)
{
	assert_and_return_r(canRun(step), {});

	const RowRenames renames = step == Step::Apply ? renamesFor(step) : std::exchange(_undo, {});
	std::vector<BatchRenameItem> items;
	items.reserve(renames.size());
	for (const auto& [row, name] : renames)
		items.push_back({ .source = _rows[row].path, .newName = name });

	const auto results = batchRename(items, shouldContinue);

	RowRenames renamed;
	std::vector<std::pair<QString, QString>> renamedPaths;
	for (size_t i = 0; i < results.size(); ++i)
	{
		const auto& [rowIndex, name] = renames[i];
		Row& row = _rows[rowIndex];
		const std::optional<InlineRenameResult>& result = results[i];
		row.failure.reset();
		if (!result)
			row.outcome = Outcome::Cancelled;
		else if (result->status == InlineRenameStatus::Renamed)
		{
			renamed.emplace_back(rowIndex, row.path.name());
			QString oldPath = row.item.fullAbsolutePath();
			row.path = row.path.parent().child(name);
			row.item = CFileSystemObject{ row.path.value() };
			row.outcome = Outcome::Renamed;
			renamedPaths.emplace_back(std::move(oldPath), row.item.fullAbsolutePath());
		}
		else if (result->status == InlineRenameStatus::NothingToDo)
			row.outcome = Outcome::Unchanged;
		else
		{
			row.outcome = Outcome::Failed;
			row.failure = *result;
		}
	}

	if (step == Step::Apply)
		_undo = std::move(renamed);
	// Until the rules change: a click queued during the renaming must not start another one
	_showingOutcomes = true;

	updatePreview();
	return renamedPaths;
}

void CBatchRenameSession::updatePreview()
{
	const CBatchRenamePattern pattern{ _rules };
	_findPatternError = pattern.findPatternError();

	// Case-folded where filesystems usually ignore case
	const auto targetKey = [](const Row& row) {
		QString key = row.path.parent().value() % QLatin1Char('/') % row.newName;
#if defined(_WIN32) || defined(__APPLE__)
		key = key.toCaseFolded();
#endif
		return key;
	};

	QHash<QString, int> targetCounts;
	for (size_t i = 0; i < _rows.size(); ++i)
	{
		Row& row = _rows[i];
		row.newName = pattern.newName(sourceOf(row.item, row.path), static_cast<int>(i));
		++targetCounts[targetKey(row)];
	}

	_anyRenamable = false;
	_anyError = !_findPatternError.isEmpty();
	for (Row& row : _rows)
	{
		row.nameRejection = NameRejection::None;
		if (row.newName == row.path.name())
			row.state = PreviewState::Unchanged;
		else if (const NameRejection rejection = checkNewEntryName(row.newName); rejection != NameRejection::None)
		{
			row.state = PreviewState::InvalidName;
			row.nameRejection = rejection;
		}
		else if (targetCounts.value(targetKey(row)) > 1)
			row.state = PreviewState::NameTaken;
		else
			row.state = PreviewState::Renamable;

		_anyRenamable |= row.state == PreviewState::Renamable;
		_anyError |= row.state == PreviewState::InvalidName || row.state == PreviewState::NameTaken;
	}
}

CBatchRenameSession::RowRenames CBatchRenameSession::renamesFor(const Step step) const
{
	if (step == Step::Undo)
		return _undo;

	RowRenames renames;
	for (size_t i = 0; i < _rows.size(); ++i)
	{
		if (_rows[i].state != PreviewState::Unchanged)
			renames.emplace_back(i, _rows[i].newName);
	}

	return renames;
}

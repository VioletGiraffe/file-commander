#include "cfoldercomparison.h"

// Submodule includes
#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"


DISABLE_COMPILER_WARNINGS
#include <QDir>
#include <QObject>
RESTORE_COMPILER_WARNINGS

#include <utility>

CFolderComparison::CFolderComparison(QString leftRoot, QString rightRoot) :
	_leftRoot{ std::move(leftRoot) },
	_rightRoot{ std::move(rightRoot) }
{
	_thread.start([this](const std::atomic<bool>& cancelled) {
		// Also on a throw, which the thread only logs: the UI waits for this
		EXEC_ON_SCOPE_EXIT([this] { _finished = true; });

		// Folded pairing: the two panels routinely sit on filesystems that disagree on case or Unicode normalization
		FolderComparisonResult result = compareFolders(_leftRoot, _rightRoot, PairingMode::FoldCaseAndNormalization, cancelled,
			[this](const int percent) { _percent = percent; });
		if (!result.aborted)
			_result = std::move(result);
	});
}

std::optional<int> CFolderComparison::percent() const noexcept
{
	const int percent = _percent;
	return percent >= 0 ? std::optional<int>{ percent } : std::nullopt;
}

bool CFolderComparison::finished()
{
	if (!_finished)
		return false;

	_thread.join();
	return true;
}

void CFolderComparison::cancel() noexcept
{
	_thread.requestCancellation();
}

const std::optional<FolderComparisonResult>& CFolderComparison::result() const noexcept
{
	assert_r(_finished);
	return _result;
}

CFolderComparison::EntryLocation CFolderComparison::locationOf(const FolderComparisonEntry& entry) const
{
	const bool onTheRightOnly = entry.status == EntryDifference::OnlyInRight;
	return { onTheRightOnly ? Panel::RightPanel : Panel::LeftPanel, QDir{ onTheRightOnly ? _rightRoot : _leftRoot }.absoluteFilePath(entry.relativePath) };
}

QString folderComparisonStatusText(const EntryDifference status)
{
	switch (status)
	{
	case EntryDifference::OnlyInLeft: return QObject::tr("Only on the left");
	case EntryDifference::OnlyInRight: return QObject::tr("Only on the right");
	case EntryDifference::Different: return QObject::tr("Different");
	case EntryDifference::ComparisonFailed: return QObject::tr("Could not be read");
	}

	return {};
}

QString folderComparisonSummary(const FolderComparisonResult& result)
{
	return result.differences.empty()
		? QObject::tr("The folders are identical: %1 files compared byte by byte.").arg(result.identicalFiles)
		: QObject::tr("Differences: %1. Identical files: %2.").arg(result.differences.size()).arg(result.identicalFiles);
}

#pragma once

#include "compiler/compiler_warnings_control.h"
#include "filecomparator/foldercomparison.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <optional>

class CController;

// Compares the two trees on a worker thread behind a cancellable progress dialog, keeping the UI responsive for what
// can be a long read of both volumes. Empty if the user cancelled.
[[nodiscard]] std::optional<FolderComparisonResult> runFolderComparison(QWidget* parent, const QString& leftRoot, const QString& rightRoot);

class CFolderComparisonWindow final : public QMainWindow
{
public:
	CFolderComparisonWindow(QWidget* parent, const FolderComparisonResult& result, const QString& leftRoot,
		const QString& rightRoot, CController& controller);
};

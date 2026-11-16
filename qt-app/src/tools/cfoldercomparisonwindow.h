#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class CController;
class CFolderComparison;

// Shows the comparison's progress in a cancellable dialog until it finishes. False if the user cancelled.
[[nodiscard]] bool waitForFolderComparison(QWidget* parent, CFolderComparison& comparison);

class CFolderComparisonWindow final : public QMainWindow
{
public:
	// Requires a comparison that finished with a result
	CFolderComparisonWindow(QWidget* parent, const CFolderComparison& comparison, CController& controller);
};

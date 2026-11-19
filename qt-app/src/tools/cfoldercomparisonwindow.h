#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class CController;
class CFolderComparison;

class CFolderComparisonWindow final : public QMainWindow
{
public:
	// Requires a comparison that finished with a result
	CFolderComparisonWindow(QWidget* parent, const CFolderComparison& comparison, CController& controller);
};

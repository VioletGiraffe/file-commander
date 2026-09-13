#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QSplitter>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <vector>

class CCommandOutputPane;

// Runs command-line commands, each with its output in its own pane, side by side; see doc/process-launching.md.
// Hidden while it has no panes.
class CCommandOutputArea final : public QSplitter
{
public:
	explicit CCommandOutputArea(QWidget* parent = nullptr);
	~CCommandOutputArea() override;

	void run(const QString& command, const QString& workingDir);

	[[nodiscard]] QStringList runningCommands() const;
	void terminateRunningCommands();

private:
	struct RunningCommand;

	// Reuses a reusable pane, or creates one
	void showOutputPaneFor(RunningCommand& command);
	[[nodiscard]] CCommandOutputPane* createPane();
	void closePane(CCommandOutputPane* pane);
	void removeCommand(const RunningCommand* command);

	// From launch until the shell has exited. A running command's pane is never reusable, so it is never closed or claimed.
	std::vector<std::unique_ptr<RunningCommand>> _commands;
	std::vector<CCommandOutputPane*> _panes;
};

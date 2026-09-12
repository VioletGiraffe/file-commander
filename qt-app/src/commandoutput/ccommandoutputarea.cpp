#include "ccommandoutputarea.h"
#include "ccommandoutputpane.h"
#include "shell/cshellcommand.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

// A command that finishes silently within this never shows a pane
constexpr int CLAIM_DELAY_MS = 250;

}

struct CCommandOutputArea::RunningCommand
{
	RunningCommand(const QString& command, const QString& workingDir) : shell{ command, workingDir } {}

	CShellCommand shell;
	CCommandOutputPane* pane = nullptr; // Null until the command prints or outlives CLAIM_DELAY_MS
	QTimer claimTimer;
};

CCommandOutputArea::CCommandOutputArea(QWidget* parent) :
	QSplitter(Qt::Horizontal, parent)
{
	setChildrenCollapsible(false);
	hide();
}

CCommandOutputArea::~CCommandOutputArea() = default;

void CCommandOutputArea::run(const QString& command, const QString& workingDir)
{
	RunningCommand& runningCommand = *_commands.emplace_back(std::make_unique<RunningCommand>(command, workingDir));

	runningCommand.shell.onOutput = [this, &runningCommand](const QString& text) {
		if (!runningCommand.pane)
			showOutputPaneFor(runningCommand);
		runningCommand.pane->appendOutput(text);
	};

	runningCommand.shell.onFinished = [this, &runningCommand](int exitCode, bool normalExit) {
		runningCommand.claimTimer.stop();
		if (runningCommand.pane)
			runningCommand.pane->markFinished(exitCode, normalExit);

		// The shell's finished signal is still being delivered, and removing the command destroys its sender
		QMetaObject::invokeMethod(this, [this, finishedCommand = &runningCommand] { removeCommand(finishedCommand); }, Qt::QueuedConnection);
	};

	runningCommand.claimTimer.setSingleShot(true);
	connect(&runningCommand.claimTimer, &QTimer::timeout, this, [this, &runningCommand] {
		if (!runningCommand.pane)
			showOutputPaneFor(runningCommand);
	});

	if (!runningCommand.shell.start())
	{
		showOutputPaneFor(runningCommand);
		runningCommand.pane->markFailedToStart();
		removeCommand(&runningCommand);
		return;
	}

	runningCommand.claimTimer.start(CLAIM_DELAY_MS);
}

QStringList CCommandOutputArea::runningCommands() const
{
	QStringList commands;
	for (const auto& command : _commands)
	{
		if (command->shell.isRunning())
			commands.push_back(command->shell.command());
	}

	return commands;
}

void CCommandOutputArea::terminateRunningCommands()
{
	for (const auto& command : _commands)
		command->shell.terminateTree();
}

void CCommandOutputArea::showOutputPaneFor(RunningCommand& command)
{
	assert_debug_only(!command.pane);

	const auto reusable = std::find_if(_panes.cbegin(), _panes.cend(), [](const CCommandOutputPane* pane) { return pane->isReusable(); });
	command.pane = reusable != _panes.cend() ? *reusable : createPane();
	command.pane->attach(command.shell.command());
	show();
}

CCommandOutputPane* CCommandOutputArea::createPane()
{
	auto* pane = new CCommandOutputPane(this);
	pane->onCloseRequested = [this, pane] { closePane(pane); };
	addWidget(pane);
	_panes.push_back(pane);
	return pane;
}

void CCommandOutputArea::closePane(CCommandOutputPane* pane)
{
	assert_debug_only(std::none_of(_commands.cbegin(), _commands.cend(), [pane](const auto& command) {
		return command->pane == pane && command->shell.isRunning();
	}));

	std::erase(_panes, pane);
	pane->hide();
	pane->deleteLater();
	setVisible(!_panes.empty());
}

void CCommandOutputArea::removeCommand(const RunningCommand* command)
{
	std::erase_if(_commands, [command](const std::unique_ptr<RunningCommand>& entry) { return entry.get() == command; });
}

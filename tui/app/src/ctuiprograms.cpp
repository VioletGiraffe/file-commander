#include "ctuiprograms.h"
#include "ctuiapplication.h"
#include "ctuidialogs.h"

#include "filesystemhelperfunctions.h"
#include "shell/cshell.h"

DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <initializer_list>
#include <utility>

// The program and its arguments from the first of the environment variables that holds a command, else the first fallback program found.
// The program is its full path when PATH has it: Windows completes a bare name only with ".exe", which misses "more.com".
// Empty when there is none.
[[nodiscard]] static QStringList commandFromEnvironment(std::initializer_list<const char*> variables, std::initializer_list<const char*> fallbackPrograms)
{
	for (const char* variable : variables)
		if (QStringList command = QProcess::splitCommand(qEnvironmentVariable(variable)); !command.isEmpty())
		{
			if (QString path = QStandardPaths::findExecutable(command.front()); !path.isEmpty())
				command.front() = std::move(path);
			return command;
		}

	for (const char* program : fallbackPrograms)
		if (QString path = QStandardPaths::findExecutable(QString::fromLatin1(program)); !path.isEmpty())
			return { std::move(path) };

	return {};
}

// Runs the command with the file's path as its last argument, in the file's folder
static void runCommandOnFile(QStringList command, const QString& filePath)
{
	const QString program = command.takeFirst();
	command.push_back(toNativeSeparators(filePath));
	if (auto ran = CTuiApplication::runInTerminal(program, command, QFileInfo(filePath).absolutePath(), false); !ran)
		showMessage(QStringLiteral("Failed to start %1").arg(program), ran.error());
}

void viewFile(const QString& filePath)
{
#ifdef _WIN32
	const QStringList command = commandFromEnvironment({ "PAGER" }, { "more" });
#else
	const QStringList command = commandFromEnvironment({ "PAGER" }, { "less", "more" });
#endif
	if (command.isEmpty())
		showMessage(QStringLiteral("No pager found"), QStringLiteral("Set the PAGER environment variable to a pager program."));
	else
		runCommandOnFile(command, filePath);
}

void editFile(const QString& filePath)
{
#ifdef _WIN32
	const QStringList command = commandFromEnvironment({ "VISUAL", "EDITOR" }, { "edit" });
	const QString noEditorHint = QStringLiteral("Install Microsoft Edit (winget install Microsoft.Edit),\nor set the VISUAL or EDITOR environment variable to an editor program.");
#else
	const QStringList command = commandFromEnvironment({ "VISUAL", "EDITOR" }, { "nano", "vi" });
	const QString noEditorHint = QStringLiteral("Set the VISUAL or EDITOR environment variable to an editor program.");
#endif
	if (command.isEmpty())
		showMessage(QStringLiteral("No editor found"), noEditorHint);
	else
		runCommandOnFile(command, filePath);
}

void runShell(const QString& folder)
{
#ifdef _WIN32
	const char* const variable = "COMSPEC";
	const QString fallback = QStringLiteral("cmd.exe");
#else
	const char* const variable = "SHELL";
	const QString fallback = QStringLiteral("/bin/sh");
#endif
	const QString shell = qEnvironmentVariableIsEmpty(variable) ? fallback : qEnvironmentVariable(variable);
	if (auto ran = CTuiApplication::runInTerminal(shell, {}, folder, false); !ran)
		showMessage(QStringLiteral("Failed to start %1").arg(shell), ran.error());
}

void openDocument(const QString& filePath)
{
#ifdef _WIN32
	if (auto opened = OsShell::openWithDefaultApplication(filePath, nullptr); !opened)
		showMessage(QStringLiteral("Failed to open the file"), opened.error());
#else
#ifdef __APPLE__
	const bool desktopShown = qEnvironmentVariableIsEmpty("SSH_CONNECTION"); // Over SSH the desktop is the Mac's, not the user's
	const QString opener = QStringLiteral("open");
#else
	const bool desktopShown = !qEnvironmentVariableIsEmpty("DISPLAY") || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
	const QString opener = QStringLiteral("xdg-open");
#endif
	if (!desktopShown)
	{
		viewFile(filePath);
		return;
	}

	QProcess process;
	process.setProgram(opener);
	process.setArguments({ filePath });
	// The opener and the application it starts would write over the panels
	process.setStandardInputFile(QProcess::nullDevice());
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());
	if (!process.startDetached())
		showMessage(QStringLiteral("Failed to start %1").arg(opener), process.errorString());
#endif
}

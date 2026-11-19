#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace OsShell
{
	// The first installed terminal the app knows of; an application name on macOS, a command line elsewhere.
	// {dir} in the command line stands for the folder to open.
	[[nodiscard]] QString defaultTerminalCommand();
	// The terminal from the settings; defaultTerminalCommand when none is set
	[[nodiscard]] QString terminalCommand();

	// Opens terminalCommand in `folder`. `admin` elevates on Windows, fails on Linux and FreeBSD, and is ignored on macOS.
	// The error is the reason for the failure, never empty.
	[[nodiscard]] std::expected<void, QString> openTerminal(QString folder, bool admin);

#ifndef _WIN32
	// Both run `commandLine` through sh in `workingDir`, untracked. The error is the reason for the failure, never empty.
	// In a new window of terminalCommand. `keepOpen`: the user's shell takes over once the line finishes.
	[[nodiscard]] std::expected<void, QString> runCommandLineInTerminal(const QString& commandLine, const QString& workingDir, bool keepOpen);
	// Without a window. Fails when sh cannot find the line's leading command; sh's errors past that are not reported.
	[[nodiscard]] std::expected<void, QString> runCommandLineDetached(const QString& commandLine, const QString& workingDir);
	// Starts `program` in `workingDir`, untracked and without sh. The error is the reason for the failure, never empty.
	[[nodiscard]] std::expected<void, QString> startDetached(const QString& program, QStringList arguments, const QString& workingDir);
#endif

#ifdef __APPLE__
	struct ApplicationBundle
	{
		QString name; // Localized
		QString path;
	};

	// The applications that open every one of `paths`, the first path's default application first
	[[nodiscard]] std::vector<ApplicationBundle> applicationsToOpen(const QStringList& paths);
	// Returns at once. When the launch fails, `onFailure` gets the reason, never empty, on the main thread.
	void openInApplication(const QStringList& paths, const QString& applicationPath, std::function<void(const QString& reason)> onFailure);
#endif

#ifdef _WIN32
	// Explorer's menus open at the mouse cursor.
	// `objects` must all be in one folder.
	void openShellContextMenuForObjects(const std::vector<std::wstring>& objects, void * parentWindow);
	bool recycleBinContextMenu(void * parentWindow);

	bool copyObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
	bool cutObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
#endif

	std::wstring toolTip(std::wstring itemPath);

	struct TrashFailure
	{
		QString path;
		QString reason;
	};

#ifdef _WIN32
	// Through the shell, which shows its own progress, prompts and errors
	bool deleteItems(const std::vector<std::wstring>& items, bool moveToTrash = true, void *parentWindow = nullptr);
#elif defined __APPLE__
	// Moves each item to the trash, continuing past failures. Empty when every item was moved.
	// Never call it on the main thread: it waits for a completion that may be delivered there.
	[[nodiscard]] std::vector<TrashFailure> moveItemsToTrashThroughNSWorkspace(const QStringList& paths);
#endif
	// Shows no UI. Moves each item to the trash, continuing past failures. Empty when every item was moved.
	[[nodiscard]] std::vector<TrashFailure> moveItemsToTrashThroughQt(const QStringList& paths);

#if defined __linux__ || defined __FreeBSD__
	// The user's freedesktop.org trash folders that exist: the home trash, and those on `volumeRoots` that the spec lets this user use
	[[nodiscard]] QStringList trashFolders(const QStringList& volumeRoots);

	struct TrashContents
	{
		QStringList items; // The trashed files and folders
		QStringList records; // The .trashinfo files, and each trash's cache of folder sizes
	};
	// What emptying `trashFolders` deletes
	[[nodiscard]] TrashContents trashContents(const QStringList& trashFolders);
#endif

	struct ProgramInvocation
	{
		QString programPath;
		QString arguments; // A raw string: the program parses its own command line
		QString workingDir; // Empty: the app's current directory
	};

	// The program and arguments when `commandLine` is only a GUI program and its arguments, so it can launch without the shell.
	// Empty when the line needs the shell or names no GUI program.
	// Always empty off Windows: nothing marks a program as GUI.
	// A bare cmd, without arguments, also qualifies: it gets its own console window.
	[[nodiscard]] std::optional<ProgramInvocation> guiProgramInvocation(const QString& commandLine, const QString& workingDir);

#ifdef _WIN32
	// guiProgramInvocation extended to any file the shell finds: a console program, a shortcut, a document.
	// Empty for a batch file: cmd runs it.
	[[nodiscard]] std::optional<ProgramInvocation> directInvocation(const QString& commandLine, const QString& workingDir);
#endif

	// The executable `commandLine` starts, found where the shell looks for it. Empty when not found, and for a shell built-in.
	// An empty `workingDir` stands for the app's current directory.
	[[nodiscard]] QString commandLineProgramPath(const QString& commandLine, const QString& workingDir);

	// `arguments` is quoted for the platform's shell: passed as is on Windows, split by sh's rules elsewhere.
	// An empty `workingDir` keeps the app's current directory.
	// The error is the reason for the failure, never empty; a cancelled elevation prompt is not a failure.
	[[nodiscard]] std::expected<void, QString> runExecutable(const QString& command, const QString& arguments, const QString& workingDir);

#ifdef _WIN32
	[[nodiscard]] std::expected<void, QString> runExe(const QString& command, const QString& arguments, const QString& workingDir, bool asAdmin = false);

	// Opens the file with its type's default verb, as Explorer's double-click does, with the file's folder as working folder.
	// For a type with no program the shell asks which one to use.
	// The error is the reason for the failure, never empty; cancelling a prompt the shell shows is not a failure.
	[[nodiscard]] std::expected<void, QString> openWithDefaultApplication(const QString& path, void* parentWindow);
#else
	// Whether exec can start the file: a native binary, or a script starting with "#!". Reads the file's first bytes.
	[[nodiscard]] bool isProgramFile(const QString& path);
#endif

	bool isInPath(const QString& fileName);
} // namespace OsShell

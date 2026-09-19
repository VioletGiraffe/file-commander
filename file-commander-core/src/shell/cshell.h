#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace OsShell
{
	std::pair<QString /* exe path */, QString /* args */> shellExecutable();

	// Pos must be global
	bool openShellContextMenuForObjects(const std::vector<std::wstring>& objects, int xPos, int yPos, void * parentWindow);

	bool copyObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
	bool cutObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
	bool pasteFilesAndFoldersFromClipboard(std::wstring destFolder, void * parentWindow);

	std::wstring toolTip(std::wstring itemPath);

#if defined _WIN32 || defined __APPLE__
	// Only implemented where the OS shell provides deletion; elsewhere deletion goes through the internal job (see deletionBackendFor)
	bool deleteItems(const std::vector<std::wstring>& items, bool moveToTrash = true, void *parentWindow = nullptr);
#endif

	bool recycleBinContextMenu(int xPos, int yPos, void * parentWindow);

	struct ProgramInvocation
	{
		QString programPath;
		QString arguments; // A raw string: the program parses its own command line
		QString workingDir; // Empty: the app's current directory
	};

	enum class GuiProgramCheckError
	{
		ExecutableTypeUnknown, // SHGetFileInfo reports nothing: a non-executable file, or the query failed
		UnsupportedPlatform    // Nothing marks a program as GUI
	};

	// The program and arguments when `commandLine` is only a GUI program and its arguments, so it can launch without the shell.
	// Empty when the line needs the shell or names no GUI program.
	// A bare cmd, without arguments, also qualifies: it gets its own console window.
	[[nodiscard]] std::expected<std::optional<ProgramInvocation>, GuiProgramCheckError> guiProgramInvocation(const QString& commandLine, const QString& workingDir);

	// An empty `workingDir` keeps the app's current directory.
	// The error is the reason for the failure, never empty; a cancelled elevation prompt is not a failure.
	[[nodiscard]] std::expected<void, QString> runExecutable(const QString& command, const QString& arguments, const QString& workingDir);

#ifdef _WIN32
	[[nodiscard]] std::expected<void, QString> runExe(const QString& command, const QString& arguments, const QString& workingDir, bool asAdmin = false);
#endif

	bool isInPath(const QString& fileName);
} // namespace OsShell

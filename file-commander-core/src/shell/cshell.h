#pragma once

#include <string>
#include <vector>
#include <utility>

class QString;

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
	// Only implemented where the OS shell provides deletion; elsewhere deletion goes through the internal job
	// (see deletionBackendFor). doc/TODO.md tracks extending trash support to the other platforms.
	bool deleteItems(const std::vector<std::wstring>& items, bool moveToTrash = true, void *parentWindow = nullptr);
#endif

	bool recycleBinContextMenu(int xPos, int yPos, void * parentWindow);

	void executeShellCommand(const QString& command, const QString& workingDir);

	bool runExecutable(const QString& command, const QString& arguments, const QString& workingDir);

#ifdef _WIN32
	bool runExe(const QString& command, const QString& arguments, const QString& workingDir, bool asAdmin = false);
#endif

	bool isInPath(const QString& fileName);
} // namespace OsShell

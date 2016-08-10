#ifndef CSHELLMENU_H
#define CSHELLMENU_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <string>
#include <vector>

class CShell
{
public:
	static QString shellExecutable();

	// Pos must be global
	static bool openShellContextMenuForObjects(const std::vector<std::wstring>& objects, int xPos, int yPos, void * parentWindow);

	static bool copyObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
	static bool cutObjectsToClipboard(const std::vector<std::wstring>& objects, void * parentWindow);
	static bool pasteFromClipboard(std::wstring destFolder, void * parentWindow);

	static std::wstring toolTip(std::wstring itemPath);

	static bool deleteItems(const std::vector<std::wstring>& items, bool moveToTrash = true, void *parentWindow = 0);

	static bool recycleBinContextMenu(int xPos, int yPos, void * parentWindow);

	static void executeShellCommand(const QString& command, const QString& workingDir);
};

#endif // CSHELLMENU_H

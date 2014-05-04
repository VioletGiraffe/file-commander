#ifndef CSHELLMENU_H
#define CSHELLMENU_H

#include <string>
#include <vector>

class QString;

class CShell
{
public:
	// Pos must be global
	static bool openShellContextMenuForObjects(std::vector<std::wstring> objects, int xPos, int yPos, void * parentWindow);

	static std::wstring toolTip(std::wstring itemPath);

	static bool deleteItems(std::vector<std::wstring> items, bool moveToTrash = true, void *parentWindow = 0);

	static bool recycleBinContextMenu(int xPos, int yPos, void * parentWindow);

	static void executeShellCommand(const QString& command, const QString& workingDir);
};

#endif // CSHELLMENU_H

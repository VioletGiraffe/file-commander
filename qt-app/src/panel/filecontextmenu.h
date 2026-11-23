#pragma once

#include "cfilesystemobject.h"
#include "diskenumerator/volumeinfo.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <vector>

class CShellOperationRunner;
class QWidget;

// The context menu of files, folders and volumes: Explorer's on Windows.
// Elsewhere it holds the system's actions that the app has no command of its own for.
// Both open at the mouse cursor.
namespace FileContextMenu {

// `items` may be in different folders; without items the menu is for `folder` itself.
// `enterFolder` shows a folder in the panel: Show Package Contents passes it the bundle.
// True when a verb of Explorer's menu was invoked, so files may have changed; always false elsewhere.
[[nodiscard]] bool showForItems(const std::vector<CFileSystemObject>& items, const QString& folder, QWidget* parent, const std::function<void(const QString& folderPath)>& enterFolder);

// Eject runs on `shellOperations`
void showForVolume(const VolumeInfo& volume, QWidget* parent, CShellOperationRunner& shellOperations);

}

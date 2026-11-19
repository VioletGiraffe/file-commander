#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QPoint>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>

class QWidget;

// The macOS services of the file context menu: Finder, the Share menu, Quick Look and ejecting volumes
namespace MacShell {

// A Finder window with `paths` selected in their folder
void showItemsInFinder(const QStringList& paths);
// A Finder window showing `folder`
void showFolderInFinder(const QString& folder);
// Finder's Get Info window for each of `paths`, through AppleScript: the first use asks the user to let the app control Finder.
// Returns at once; a failure is shown over `parent`, unless it is gone by then.
void showInfoInFinder(const QStringList& paths, QWidget* parent);
// The system's menu of the services that share `paths`, at `globalPosition` in `widget`'s window
void showShareMenu(const QStringList& paths, const QWidget* widget, QPoint globalPosition);
// The Quick Look panel for `paths`, controlled from `widget`'s window
void showQuickLook(const QStringList& paths, const QWidget* widget);

// Unmounts the volume at `rootPath` and ejects its media. Blocks until done: never call it on the UI thread.
// The error is the reason for the failure, never empty.
[[nodiscard]] std::expected<void, QString> ejectVolume(const QString& rootPath);

}

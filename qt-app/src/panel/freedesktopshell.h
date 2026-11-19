#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <expected>

class QWidget;

// The desktop's services over D-Bus, for Linux and FreeBSD: the file manager's org.freedesktop.FileManager1, and UDisks2
namespace FreedesktopShell {

// Whether a file manager provides org.freedesktop.FileManager1, running or activatable
[[nodiscard]] bool fileManagerAvailable();

// Each returns at once and shows a failure over `parent`, unless `parent` is gone by then
void showFoldersInFileManager(const QStringList& folders, QWidget* parent);
// The file manager selects `paths` in their folder
void showItemsInFileManager(const QStringList& paths, QWidget* parent);
void showPropertiesInFileManager(const QStringList& paths, QWidget* parent);

// Unmounts the filesystem on `devicePath` through UDisks2, locks the LUKS volume it may lie on, and ejects, powers off or detaches
// the device. The system may ask the user for authorization meanwhile.
// Blocks until done: never call it on the UI thread. The error is the reason for the failure, never empty.
[[nodiscard]] std::expected<void, QString> ejectBlockDevice(const QString& devicePath);

}

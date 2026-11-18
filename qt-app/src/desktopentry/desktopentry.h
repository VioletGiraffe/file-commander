#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QLatin1StringView>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <expected>

// The user's applications menu entry for the AppImage this process runs from
namespace DesktopEntry {

// The desktop file ID: installer/linux/file_commander.desktop is named after it
inline constexpr QLatin1StringView id{ "file_commander" };

[[nodiscard]] bool runningFromAppImage();
// False for an entry that launches another AppImage file
[[nodiscard]] bool installedForThisAppImage();

// Requires runningFromAppImage(). Replaces an entry for another AppImage file.
[[nodiscard]] std::expected<void, QString> install();
[[nodiscard]] std::expected<void, QString> uninstall();

}

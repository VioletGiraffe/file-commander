#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QString>
RESTORE_COMPILER_WARNINGS

// The viewer's help context: the hint row and the key bar show the viewer's keys while it is open
inline constexpr ushort hcViewer = 1000;

// The viewer's commands, which its keys and the items of the hint row and the key bar send
inline constexpr ushort cmFindInViewer = 2400, cmFindNextInViewer = 2401, cmFindPreviousInViewer = 2402, cmToggleLineWrap = 2403, cmFirstViewAs = 2404;

// In the order of their keys, 1 to 6, as in the GUI's text viewer
enum class ViewAs : ushort { DetectedEncoding, Latin1, SystemLocale, Utf8, Utf16, Hex };
inline constexpr int viewAsCount = (int)ViewAs::Hex + 1;

// The command that shows the file as viewAs
[[nodiscard]] constexpr ushort cmViewAs(const ViewAs viewAs) noexcept
{
	return (ushort)(cmFirstViewAs + (ushort)viewAs);
}

// Shows the file over the panels and the menu bar until Esc closes it. A file that cannot be read gets a message instead.
void viewFile(const QString& filePath);

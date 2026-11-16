#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// Each shows a message when its program cannot be started

// In the editor, until it exits: VISUAL or EDITOR, else a fallback found in PATH
void editFile(const QString& filePath);
// The user's shell in the folder, until it exits
void runShell(const QString& folder);
// Opens the file with its default application where a desktop can show it, else in the viewer
void openDocument(const QString& filePath);

#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// Each shows a message when its program cannot be started

// In the pager, until it exits: PAGER, else a fallback found in PATH
void viewFile(const QString& filePath);
// In the editor, until it exits: VISUAL or EDITOR, else a fallback found in PATH
void editFile(const QString& filePath);
// The user's shell in the folder, until it exits
void runShell(const QString& folder);
// Opens the file with its default application where a desktop can show it, else in the pager
void openDocument(const QString& filePath);

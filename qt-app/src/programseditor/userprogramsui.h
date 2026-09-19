#pragma once

#include "userprograms/userprograms.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QKeySequence>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stddef.h>

// Ctrl+Shift+F1 to F12 by position in the list; empty past the 12th
[[nodiscard]] QKeySequence userProgramShortcut(size_t position);

// The icon of the executable the command line starts; a generic file icon when it is not found
[[nodiscard]] QIcon userProgramIcon(const UserProgram& program);

[[nodiscard]] QString placeholderErrorText(PlaceholderError error);
[[nodiscard]] QString commandLineTooLongText(qsizetype length, qsizetype maxLength);

#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// The version define below is parsed by qt-app.pro at qmake time - keep it a plain one-line string literal
#define VERSION_STRING "0.9.9.7"
#define REPO_NAME QLatin1String("VioletGiraffe/file-commander")

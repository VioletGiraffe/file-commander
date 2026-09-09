#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <vector>

class QWidget;

void showAboutDialog(QWidget* parent, const std::vector<QString>& activePluginNames);

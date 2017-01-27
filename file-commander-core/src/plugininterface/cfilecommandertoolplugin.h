#pragma once

#include "cfilecommanderplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QString>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>
#include <vector>

class CFileCommanderToolPlugin : public CFileCommanderPlugin
{
public:
	CFileCommanderToolPlugin() = default;

	PluginType type() override;

};

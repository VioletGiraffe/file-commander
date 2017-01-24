#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	CFileComparisonPlugin() = default;

	QString name() const override;
};

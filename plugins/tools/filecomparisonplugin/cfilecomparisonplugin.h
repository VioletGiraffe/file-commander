#pragma once

#include "plugininterface/cfilecommandertoolplugin.h"

class CFileComparisonPlugin : public CFileCommanderToolPlugin
{
public:
	CFileComparisonPlugin();

	QString name() const override;
};

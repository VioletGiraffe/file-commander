#include "cfilecomparisonplugin.h"

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin()
{
	_commands.emplace_back("Compare files by contents");
}

QString CFileComparisonPlugin::name() const
{
	return QObject::tr("File comparison plugin");
}

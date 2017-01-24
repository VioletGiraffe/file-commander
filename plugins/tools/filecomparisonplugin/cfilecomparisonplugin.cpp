#include "cfilecomparisonplugin.h"

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

QString CFileComparisonPlugin::name() const
{
	return QObject::tr("File comparison plugin");
}

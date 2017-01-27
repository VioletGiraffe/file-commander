#include "cfilecomparisonplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin()
{
}

QString CFileComparisonPlugin::name() const
{
	return QObject::tr("File comparison plugin");
}

void CFileComparisonPlugin::proxySet()
{
	CPluginProxy::MenuTree menu("Compare files by contents", [this]() {
		compareSelectedFiles();
	});

	_proxy->createToolMenuEntries(menu);
}

void CFileComparisonPlugin::compareSelectedFiles()
{
}

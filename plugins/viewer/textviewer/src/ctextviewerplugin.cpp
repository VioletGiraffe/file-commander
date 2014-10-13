#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"

CFileCommanderPlugin * createPlugin()
{
	Q_INIT_RESOURCE(icons);
	return new CTextViewerPlugin;
}

CTextViewerPlugin::CTextViewerPlugin()
{
}

bool CTextViewerPlugin::canViewCurrentFile() const
{
	return _proxy->currentItemIsFile();
}

CPluginWindow * CTextViewerPlugin::viewCurrentFile()
{
	CTextViewerWindow * widget = new CTextViewerWindow;
	widget->loadTextFile(_proxy->currentItemPath());
	return widget;
}

QString CTextViewerPlugin::name()
{
	return "Plain text, HTML and RTF viewer plugin";
}

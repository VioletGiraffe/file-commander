#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"

CFileCommanderPlugin * createPlugin()
{
	return new CTextViewerPlugin;
}

CTextViewerPlugin::CTextViewerPlugin()
{
}

bool CTextViewerPlugin::canViewCurrentFile() const
{
	return true;
}

QWidget* CTextViewerPlugin::viewCurrentFile()
{
	CTextViewerWindow * widget = new CTextViewerWindow;
	widget->loadTextFile(currentItemPath());
	return widget;
}

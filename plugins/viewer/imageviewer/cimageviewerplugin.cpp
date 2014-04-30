#include "cimageviewerplugin.h"

CImageViewerPlugin::CImageViewerPlugin()
{
}

bool CImageViewerPlugin::canViewCurrentFile() const
{
	return false;
}

QWidget*CImageViewerPlugin::viewCurrentFile() const
{
	return nullptr;
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

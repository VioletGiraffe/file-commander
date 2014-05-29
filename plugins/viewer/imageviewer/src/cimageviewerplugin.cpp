#include "cimageviewerplugin.h"
#include "cimageviewerwidget.h"
#include "QtIncludes.h"

CImageViewerPlugin::CImageViewerPlugin()
{
}

bool CImageViewerPlugin::canViewCurrentFile() const
{
	const QString currentItem(currentItemPath());
	if (currentItem.isEmpty())
		return false;
	else
		return QImageReader(currentItem).canRead();
}

CPluginWindow* CImageViewerPlugin::viewCurrentFile()
{
	CImageViewerWidget * widget = new CImageViewerWidget;
	widget->displayImage(currentItemPath());
	return widget;
}

QString CImageViewerPlugin::name()
{
	return "Image viewer plugin";
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

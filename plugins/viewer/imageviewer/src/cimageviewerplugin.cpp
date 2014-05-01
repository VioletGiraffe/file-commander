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

QWidget* CImageViewerPlugin::viewCurrentFile()
{
	CImageViewerWidget * widget = new CImageViewerWidget;
	widget->displayImage(currentItemPath());
	widget->connect(widget, SIGNAL(closed()), SLOT(deleteLater()));
	return widget;
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

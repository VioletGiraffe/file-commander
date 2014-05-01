#include "cimageviewerplugin.h"
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
	_widget.displayImage(currentItemPath());
	return &_widget;
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

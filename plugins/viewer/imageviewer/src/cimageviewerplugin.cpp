#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"


CImageViewerPlugin::CImageViewerPlugin()
{
	_imageReader.setDecideFormatFromContent(true);
}

bool CImageViewerPlugin::canViewCurrentFile() const
{
	const QString currentItemPath = _proxy->currentItemPath();
	if (currentItemPath.isEmpty())
		return false;

	_imageReader.setFileName(currentItemPath);
	return _imageReader.canRead();
}

CPluginWindow* CImageViewerPlugin::viewCurrentFile()
{
	CImageViewerWindow * widget = new CImageViewerWindow;
	if (widget->displayImage(_proxy->currentItemPath(), _imageReader.read()))
		return widget;
	else
	{
		delete widget;
		return nullptr;
	}
}

QString CImageViewerPlugin::name() const
{
	return "Image viewer plugin";
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

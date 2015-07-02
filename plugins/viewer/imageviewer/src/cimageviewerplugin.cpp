#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QImageReader>
RESTORE_COMPILER_WARNINGS

CImageViewerPlugin::CImageViewerPlugin()
{
}

bool CImageViewerPlugin::canViewCurrentFile() const
{
	const QString currentItemPath = _proxy->currentItemPath();
	if (currentItemPath != _cachedImagePath)
	{
		_cachedImage = QImage();
		_cachedImagePath = currentItemPath;
	}

	if (currentItemPath.isEmpty())
		return false;
	
	if (!_cachedImage.isNull())
		return true;

	QImageReader imageReader(currentItemPath);
	if (!imageReader.canRead())
		return false;
	else
	{
		_cachedImage = imageReader.read();
		return !_cachedImage.isNull();
	}
}

CPluginWindow* CImageViewerPlugin::viewCurrentFile()
{
	CImageViewerWindow * widget = new CImageViewerWindow;
	if (widget->displayImage(_proxy->currentItemPath(), _cachedImage))
	{
		_cachedImage = QImage();
		return widget;
	}

	delete widget;
	return nullptr;
}

QString CImageViewerPlugin::name() const
{
	return "Image viewer plugin";
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

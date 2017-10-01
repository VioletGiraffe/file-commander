#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QImageReader>
RESTORE_COMPILER_WARNINGS

bool CImageViewerPlugin::canViewCurrentFile() const
{
	const QString currentItemPath = _proxy->currentItemPath();
	if (currentItemPath.isEmpty())
		return false;

	QImageReader reader(currentItemPath);
	reader.setDecideFormatFromContent(true);
	return reader.canRead();
}

CPluginWindow* CImageViewerPlugin::viewCurrentFile()
{
	CImageViewerWindow * widget = new CImageViewerWindow;
	if (widget->displayImage(_proxy->currentItemPath()))
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

#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

bool CImageViewerPlugin::canViewFile(const QString& /*fileName*/, const QMimeType& type) const
{
	return type.name().startsWith(QStringLiteral("image/"));
}

CPluginWindow* CImageViewerPlugin::viewFile(const QString& fileName)
{
	CImageViewerWindow * widget = new CImageViewerWindow;
	if (widget->displayImage(fileName))
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

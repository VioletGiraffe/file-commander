#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QImageReader>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

bool CImageViewerPlugin::canViewFile(const QString& fileName, const QMimeType& type) const
{
	if (type.name().startsWith(QStringLiteral("image/")))
		return true;

	QImageReader reader(fileName);
	return reader.canRead();
}

CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> CImageViewerPlugin::viewFile(const QString& fileName)
{
	auto window = WindowPtr<CImageViewerWindow>::create();

	if (window->displayImage(fileName))
	{
		window->adjustSize();
	}
	else
		window.reset();

	return window;
}

QString CImageViewerPlugin::name() const
{
	return QObject::tr("Image viewer plugin");
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

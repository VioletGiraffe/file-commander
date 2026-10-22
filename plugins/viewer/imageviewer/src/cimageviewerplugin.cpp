#include "cimageviewerplugin.h"

#include "cimageviewer.h"


DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QImageReader>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

bool CImageViewerPlugin::canViewFile(const QString& fileName, const QMimeType& type) const
{
	const QString mimeTypeName = type.name();
	if (mimeTypeName.startsWith(QStringLiteral("image/")))
		return true;

	QImageReader reader(fileName);
	return reader.canRead();
}

std::unique_ptr<CFileViewer> CImageViewerPlugin::createViewer()
{
	return std::make_unique<CImageViewer>(*_proxy);
}

QString CImageViewerPlugin::name() const
{
	return QObject::tr("Image viewer plugin");
}

QString CImageViewerPlugin::category() const
{
	return QStringLiteral("imageviewer");
}


uint32_t pluginInterfaceVersion()
{
	return PLUGIN_INTERFACE_VERSION;
}

CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

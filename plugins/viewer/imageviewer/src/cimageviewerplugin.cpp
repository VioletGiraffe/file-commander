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

CFileCommanderViewerPlugin::PluginWindowPointerType CImageViewerPlugin::viewFile(const QString& fileName)
{
	auto* window = new CImageViewerWindow;
	if (window->displayImage(fileName))
	{
	// The window needs a custom deleter because it must be deleted in the same dynamic library where it was allocated
		return PluginWindowPointerType(window, [](CPluginWindow* pluginWindow) {
			delete pluginWindow;
		});
	}

	delete window;
	return nullptr;
}

QString CImageViewerPlugin::name() const
{
	return QObject::tr("Image viewer plugin");
}


CFileCommanderPlugin* createPlugin()
{
	return new CImageViewerPlugin;
}

#include "cimageviewerplugin.h"
#include "cimageviewerwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QMimeDatabase>
RESTORE_COMPILER_WARNINGS

bool CImageViewerPlugin::canViewCurrentFile() const
{
	const QString currentItemPath = _proxy->currentItemPath();
	if (currentItemPath.isEmpty())
		return false;

	const auto mime = QMimeDatabase().mimeTypeForFile(currentItemPath, QMimeDatabase::MatchContent);
	return mime.name().startsWith(QStringLiteral("image/"));
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

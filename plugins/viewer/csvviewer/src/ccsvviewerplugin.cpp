#include "ccsvviewerplugin.h"

#include "ccsvviewerwindow.h"


DISABLE_COMPILER_WARNINGS
#include <QFileInfo>
#include <QMimeType>
RESTORE_COMPILER_WARNINGS

bool CCsvViewerPlugin::canViewFile(const QString& fileName, const QMimeType& type) const
{
	const QFileInfo fileInfo(fileName);
	const QString suffix = fileInfo.suffix();
	// The suffixes cover a system MIME database without these types
	const bool isCsv = suffix.compare(QStringLiteral("csv"), Qt::CaseInsensitive) == 0 || suffix.compare(QStringLiteral("tsv"), Qt::CaseInsensitive) == 0
		|| type.inherits(QStringLiteral("text/csv")) || type.inherits(QStringLiteral("text/tab-separated-values"));

	return isCsv && fileInfo.isFile();
}

CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> CCsvViewerPlugin::viewFile(const QString& fileName)
{
	auto window = WindowPtr<CCsvViewerWindow>::create();
	if (!window->loadFile(fileName))
		window.reset();

	return window;
}

QString CCsvViewerPlugin::name() const
{
	return QObject::tr("CSV viewer plugin");
}

QString CCsvViewerPlugin::category() const
{
	return QStringLiteral("csvviewer");
}


uint32_t pluginInterfaceVersion()
{
	return PLUGIN_INTERFACE_VERSION;
}

CFileCommanderPlugin* createPlugin()
{
	return new CCsvViewerPlugin;
}

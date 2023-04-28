#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"
#include "compiler/compiler_warnings_control.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QMimeType>
RESTORE_COMPILER_WARNINGS

CFileCommanderPlugin * createPlugin()
{
	DISABLE_COMPILER_WARNINGS
	Q_INIT_RESOURCE(icons);
	RESTORE_COMPILER_WARNINGS

	return new CTextViewerPlugin;
}

bool CTextViewerPlugin::canViewFile(const QString& fileName, const QMimeType& /*type*/) const
{
	return CFileSystemObject(fileName).isFile();
}

CFileCommanderViewerPlugin::PluginWindowPointerType CTextViewerPlugin::viewFile(const QString& fileName)
{
	QMainWindow * mainWindow = WidgetUtils::findTopLevelWindow();

	auto* window = new CTextViewerWindow(mainWindow); // Temporary workaround for https://bugreports.qt.io/browse/QTBUG-61213
	if (window->loadTextFile(fileName))
	{
		// The window needs a custom deleter because it must be deleted in the same dynamic library where it was allocated
		return PluginWindowPointerType(window, [](CPluginWindow* pluginWindow) {
			delete pluginWindow;
		});
	}

	delete window;
	return nullptr;
}

QString CTextViewerPlugin::name() const
{
	return QObject::tr("Plain text and HTML viewer plugin");
}

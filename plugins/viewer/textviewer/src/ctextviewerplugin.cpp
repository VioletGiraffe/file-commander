#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"
#include "compiler/compiler_warnings_control.h"
#include "cfilesystemobject.h"
#include "widgets/widgetutils.h"

DISABLE_COMPILER_WARNINGS
#include <QMimeType>
RESTORE_COMPILER_WARNINGS

CFileCommanderPlugin * createPlugin()
{
	DISABLE_COMPILER_WARNINGS
	Q_INIT_RESOURCE(icons);
	Q_INIT_RESOURCE(qutepart_syntax_files);
	Q_INIT_RESOURCE(qutepart_theme_data);
	RESTORE_COMPILER_WARNINGS

	return new CTextViewerPlugin;
}

bool CTextViewerPlugin::canViewFile(const QString& fileName, const QMimeType& /*type*/) const
{
	return CFileSystemObject(fileName).isFile();
}

CFileCommanderViewerPlugin::WindowPtr<CPluginWindow> CTextViewerPlugin::viewFile(const QString& fileName)
{
	QMainWindow * mainWindow = WidgetUtils::findTopLevelWindow();

	auto window = WindowPtr<CTextViewerWindow>::create(mainWindow); // Temporary workaround for https://bugreports.qt.io/browse/QTBUG-61213
	if (!window->loadTextFile(fileName))
	{
		window.reset();
	}

	return window;
}

QString CTextViewerPlugin::name() const
{
	return QObject::tr("Plain text and HTML viewer plugin");
}

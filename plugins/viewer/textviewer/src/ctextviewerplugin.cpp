#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"
#include "compiler/compiler_warnings_control.h"
#include "widgets/widgetutils.h"

#include <QMimeType>

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

CPluginWindow * CTextViewerPlugin::viewFile(const QString& fileName)
{
	QMainWindow * mainWindow = WidgetUtils::findTopLevelWindow();

	CTextViewerWindow * widget = new CTextViewerWindow(mainWindow); // Temporary workaround for https://bugreports.qt.io/browse/QTBUG-61213
	if (widget->loadTextFile(fileName))
		return widget;

	delete widget;
	return nullptr;
}

QString CTextViewerPlugin::name() const
{
	return "Plain text and HTML viewer plugin";
}

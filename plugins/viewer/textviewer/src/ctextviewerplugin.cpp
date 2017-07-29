#include "ctextviewerplugin.h"
#include "ctextviewerwindow.h"
#include "compiler/compiler_warnings_control.h"

CFileCommanderPlugin * createPlugin()
{
	DISABLE_COMPILER_WARNINGS
	Q_INIT_RESOURCE(icons);
	RESTORE_COMPILER_WARNINGS

	return new CTextViewerPlugin;
}

bool CTextViewerPlugin::canViewCurrentFile() const
{
	return _proxy->currentItem().isFile();
}

CPluginWindow * CTextViewerPlugin::viewCurrentFile()
{
	QWidget * mainWindow = nullptr;
	for (QWidget* topLevelWidget: QApplication::topLevelWidgets())
	{
		if (topLevelWidget->inherits("QMainWindow"))
		{
			mainWindow = topLevelWidget;
			break;
		}
	}

	CTextViewerWindow * widget = new CTextViewerWindow(mainWindow); // Temporary workaround for https://bugreports.qt.io/browse/QTBUG-61213
	if (widget->loadTextFile(_proxy->currentItemPath()))
		return widget;

	delete widget;
	return nullptr;
}

QString CTextViewerPlugin::name() const
{
	return "Plain text and HTML viewer plugin";
}

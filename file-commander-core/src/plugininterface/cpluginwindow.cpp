#include "cpluginwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

CPluginWindow::CPluginWindow() :
	QMainWindow(appMainWindow(), Qt::Dialog)
{
	setAttribute(Qt::WA_WindowPropagation);
}

QMainWindow* CPluginWindow::appMainWindow()
{
	QList<QWidget*> widgets = qApp->topLevelWidgets();
	foreach (QWidget* mw, widgets) {
		if (mw->objectName() == QLatin1Literal("CMainWindow")) {
			return static_cast<QMainWindow*>(mw);
		}
	}

	Q_ASSERT(false);
	return nullptr;
}

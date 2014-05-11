#include "cpluginwindow.h"

CPluginWindow::CPluginWindow(QWidget *parent) :
	QMainWindow(parent)
{
	new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
}

void CPluginWindow::closeEvent(QCloseEvent* event)
{
	if (event)
		deleteLater();

	QMainWindow::closeEvent(event);
}

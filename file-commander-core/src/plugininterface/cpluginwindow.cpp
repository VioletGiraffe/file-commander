#include "cpluginwindow.h"

CPluginWindow::CPluginWindow(QWidget *parent) :
	QMainWindow(parent),
	_bAutoDeleteOnClose(false)
{
	new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
}

bool CPluginWindow::autoDeleteOnClose() const
{
	return _bAutoDeleteOnClose;
}

void CPluginWindow::setAutoDeleteOnClose(bool autoDelete)
{
	_bAutoDeleteOnClose = autoDelete;
}

void CPluginWindow::closeEvent(QCloseEvent* event)
{
	if (event && _bAutoDeleteOnClose)
		deleteLater();

	QMainWindow::closeEvent(event);
}

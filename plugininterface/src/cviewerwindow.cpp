#include "cviewerwindow.h"

CViewerWindow::CViewerWindow(QWidget *parent) :
	QMainWindow(parent)
{
	new QShortcut(QKeySequence("Esc"), this, SLOT(close()));
}

void CViewerWindow::closeEvent(QCloseEvent* event)
{
	if (event)
		deleteLater();

	QMainWindow::closeEvent(event);
}

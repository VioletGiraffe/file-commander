#include "cpluginwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

CPluginWindow::CPluginWindow(QWidget *parent) noexcept : QMainWindow(nullptr)
{
	if (!parent)
		return;

	setFont(parent->font());
	setPalette(parent->palette());
	setStyleSheet(parent->styleSheet());
}

bool CPluginWindow::autoDeleteOnClose() const
{
	return testAttribute(Qt::WA_DeleteOnClose);
}

void CPluginWindow::setAutoDeleteOnClose(bool autoDelete)
{
	setAttribute(Qt::WA_DeleteOnClose, autoDelete);
}

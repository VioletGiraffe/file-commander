#include "cpluginwindow.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

CPluginWindow::CPluginWindow()
{
	setAttribute(Qt::WA_WindowPropagation);
}

bool CPluginWindow::autoDeleteOnClose() const
{
	return testAttribute(Qt::WA_DeleteOnClose);
}

void CPluginWindow::setAutoDeleteOnClose(bool autoDelete)
{
	setAttribute(Qt::WA_DeleteOnClose, autoDelete);
}

CPluginWindow::~CPluginWindow()
{
}


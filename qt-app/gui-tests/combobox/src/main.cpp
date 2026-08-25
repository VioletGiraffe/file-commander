#include "mainwindow.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
RESTORE_COMPILER_WARNINGS

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	MainWindow window;
	window.show();

	return app.exec();
}

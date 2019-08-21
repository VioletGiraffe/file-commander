#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
RESTORE_COMPILER_WARNINGS

int main(int argc, char *argv[])
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qInfo() << message;
	});

	qInfo() << "Built with Qt" << QT_VERSION_STR;
	qInfo() << "Running with Qt" << qVersion();
	assert_r(QStringLiteral(QT_VERSION_STR) == qVersion());

	QApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;

	w.onCreate();
	w.updateInterface();

	if (app.arguments().contains("--test-launch"))
		return 0; // Test launch succeeded

	return app.exec();
}


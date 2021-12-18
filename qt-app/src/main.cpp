#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"
#include "system/win_utils.hpp"

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

	CO_INIT_HELPER(COINIT_APARTMENTTHREADED);

	qInfo() << "Built with Qt" << QT_VERSION_STR;
	qInfo() << "Running with Qt" << qVersion();
	assert_r(QStringLiteral(QT_VERSION_STR) == qVersion());

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
	QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

	QApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	{
		QFont font = QApplication::font();
		font.setPointSizeF(font.pointSizeF() + 1);
		QApplication::setFont(font);
	}

	CMainWindow w;

	w.onCreate();
	w.updateInterface();

	if (app.arguments().contains("--test-launch"))
		return 0; // Test launch succeeded

	return app.exec();
}


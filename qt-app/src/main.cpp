#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"

DISABLE_COMPILER_WARNINGS
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QKeyEvent>
RESTORE_COMPILER_WARNINGS

class ApplicationEventFilter : public QObject
{
public:
	inline explicit ApplicationEventFilter(QObject* parent) : QObject(parent) {}
};

struct NativeEventFilter : public QAbstractNativeEventFilter
{
	inline explicit NativeEventFilter(CMainWindow& mainApplicationWindow) : _mainWindow(mainApplicationWindow) {}

	inline bool nativeEventFilter(const QByteArray & /*eventType*/, void * /*message*/, long * /*result*/) override {
		if (!_mainWindowInited)
		{
			_mainWindowInited = true;
			_mainWindow.onCreate();
			_mainWindow.updateInterface();
		}

		return false;
	}

private:
	CMainWindow& _mainWindow;
	bool _mainWindowInited = false;
};

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

	app.installEventFilter(new ApplicationEventFilter(&app));

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;

	NativeEventFilter nativeEventFilter(w);
	app.installNativeEventFilter(&nativeEventFilter);

	if (app.arguments().contains("--test-launch"))
		return 0; // Test launch succeeded

	return app.exec();
}


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

	inline bool eventFilter(QObject * /*receiver*/, QEvent * e) override
	{
		// A dirty hack to implement switching between left and right panels on Tab key press
		if (e->type() == QEvent::KeyPress)
		{
			const auto keyEvent = static_cast<QKeyEvent*>(e);
			if (keyEvent->key() == Qt::Key_Tab && CMainWindow::get())
			{
				CMainWindow::get()->tabKeyPressed();
				return true;
			}
		}

		return false;
	}
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

	w.onCreate();
	w.updateInterface();

	if (app.arguments().contains("--test-launch"))
		return 0; // Test launch succeeded

	return app.exec();
}


#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"
#include "ui/high_dpi_support.h"
#include "directoryscanner.h"

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
	inline ApplicationEventFilter(QObject* parent) : QObject(parent) {}

	inline bool eventFilter(QObject * /*receiver*/, QEvent * e) override
	{
		// A dirty hack to implement switching between left and right panels on Tab key press
		if (e->type() == QEvent::KeyPress)
		{
			QKeyEvent * keyEvent = static_cast<QKeyEvent*>(e);
			if (keyEvent->key() == Qt::Key_Tab && CMainWindow::get())
			{
				CMainWindow::get()->tabKeyPressed();
				return true;
			}
		}

		return false;
	}
};

struct NativeEventFilter : public QAbstractNativeEventFilter
{
	inline NativeEventFilter(CMainWindow& mainApplicationWindow) : _mainWindow(mainApplicationWindow) {}

	inline bool nativeEventFilter(const QByteArray & /*eventType*/, void * /*message*/, long * /*result*/) override {
		if (!_mainWindow.created())
		{
			_mainWindow.onCreate();
			_mainWindow.updateInterface();
		}

		return false;
	}

private:
	CMainWindow& _mainWindow;
};

int main(int argc, char *argv[])
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qDebug() << message;
	});

	QApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	app.installEventFilter(new ApplicationEventFilter(&app));

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	enable_high_dpi_support();

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;

	NativeEventFilter nativeEventFilter(w);
	app.installNativeEventFilter(&nativeEventFilter);

	return app.exec();
}


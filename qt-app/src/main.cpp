#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"
#include "ui/high_dpi_support.h"
#include "directoryscanner.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QKeyEvent>
RESTORE_COMPILER_WARNINGS

class CFileCommanderApplication : public QApplication
{
public:
	CFileCommanderApplication(int &argc, char **argv) : QApplication(argc, argv) {}
	inline bool notify(QObject * receiver, QEvent * e) override
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

		return QApplication::notify(receiver, e);
	}
};

int main(int argc, char *argv[])
{
	AdvancedAssert::setLoggingFunc([](const char* message){
		qDebug() << message;
	});

	CFileCommanderApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	QFontDatabase::addApplicationFont(":/fonts/Roboto Mono.ttf");

	enable_high_dpi_support();

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;
	w.updateInterface();

	const int retCode = app.exec();
	return retCode;
}


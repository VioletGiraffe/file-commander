#include "cmainwindow.h"
#include "settings/csettings.h"
#include <assert.h>

class CFileCommanderApplication : public QApplication
{
public:
	CFileCommanderApplication(int &argc, char **argv) : QApplication(argc, argv), _mainWindow (0) {}
	virtual bool notify(QObject * receiver, QEvent * e);
	void setMainWindow (CMainWindow * mainWindow) { _mainWindow = mainWindow; }
private:
	CMainWindow * _mainWindow;
};

int main(int argc, char *argv[])
{
	//_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);
	CFileCommanderApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;
	w.updateInterface();

	app.setMainWindow(&w);

	return app.exec();
}


bool CFileCommanderApplication::notify(QObject *receiver, QEvent *e)
{
	if (e && e->type() == QEvent::KeyPress)
	{
		QKeyEvent * keyEvent = dynamic_cast<QKeyEvent*>(e);
		if (keyEvent && keyEvent->key() == Qt::Key_Tab)
		{
			_mainWindow->tabKeyPressed();
			return true;
		}
	}
	return QApplication::notify(receiver, e);
}

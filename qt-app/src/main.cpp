#include "cmainwindow.h"
#include "settings/csettings.h"
#include "iconprovider/ciconprovider.h"
#include <assert.h>

class CFileCommanderApplication : public QApplication
{
public:
	CFileCommanderApplication(int &argc, char **argv) : QApplication(argc, argv) {}
	bool notify(QObject * receiver, QEvent * e) override;
	void setMainWindow (CMainWindow * mainWindow) { _mainWindow = mainWindow; }
private:
	CMainWindow * _mainWindow = nullptr;
};

int main(int argc, char *argv[])
{
	CFileCommanderApplication app(argc, argv);
	app.setOrganizationName("GitHubSoft");
	app.setApplicationName("File Commander");

	CSettings::setApplicationName(app.applicationName());
	CSettings::setOrganizationName(app.organizationName());

	CMainWindow w;
	w.updateInterface();

	app.setMainWindow(&w);

	const int retCode = app.exec();
	return retCode;
}


bool CFileCommanderApplication::notify(QObject *receiver, QEvent *e)
{
	if (e && e->type() == QEvent::KeyPress)
	{
		QKeyEvent * keyEvent = dynamic_cast<QKeyEvent*>(e);
		if (keyEvent && keyEvent->key() == Qt::Key_Tab && _mainWindow)
		{
			_mainWindow->tabKeyPressed();
			return true;
		}
	}
	return QApplication::notify(receiver, e);
}

#ifndef CFILECOMMANDERPLUGIN_H
#define CFILECOMMANDERPLUGIN_H

#include "../cfilesystemobject.h"

#include <QString>
#include <vector>

class CFileCommanderPlugin
{
public:
	enum PanelPosition {LeftPanel, RightPanel, UnknownPanel};

	CFileCommanderPlugin();
	virtual ~CFileCommanderPlugin() = 0;

// Events and data updates from core
	virtual void panelContentsChanged(PanelPosition panel, const QString& folder, std::vector<CFileSystemObject>& contents);

// Events and data updates from UI

protected:

private:

};

#endif // CFILECOMMANDERPLUGIN_H

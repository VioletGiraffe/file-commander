#include "cfilecommanderplugin.h"

CFileCommanderPlugin::CFileCommanderPlugin()
{
}

QString CFileCommanderPlugin::name()
{
	return QString();
}

CFileCommanderPlugin::~CFileCommanderPlugin()
{
}

void CFileCommanderPlugin::panelContentsChanged(CFileCommanderPlugin::PanelPosition panel, const QString &folder, std::map<qulonglong, CFileSystemObject>& contents)
{
	PanelState& state = _panelState[panel];

	state.panelContents = contents;
	state.currentFolder = folder;
}

void CFileCommanderPlugin::selectionChanged(CFileCommanderPlugin::PanelPosition panel, std::vector<qulonglong> selectedItemsHashes)
{
	PanelState& state = _panelState[panel];
	state.selectedItemsHashes = selectedItemsHashes;
}

void CFileCommanderPlugin::currentItemChanged(CFileCommanderPlugin::PanelPosition panel, qulonglong currentItemHash)
{
	PanelState& state = _panelState[panel];
	state.currentItemHash = currentItemHash;
}

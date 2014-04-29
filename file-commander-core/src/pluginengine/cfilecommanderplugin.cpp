#include "cfilecommanderplugin.h"

CFileCommanderPlugin::CFileCommanderPlugin()
{
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

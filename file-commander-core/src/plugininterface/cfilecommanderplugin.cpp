#include "cfilecommanderplugin.h"
#include <assert.h>

CFileCommanderPlugin::CFileCommanderPlugin() :
	_currentPanel(PluginUnknownPanel)
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

void CFileCommanderPlugin::currentPanelChanged(CFileCommanderPlugin::PanelPosition panel)
{
	_currentPanel = panel;
}

CFileCommanderPlugin::PanelState& CFileCommanderPlugin::currentPanelState()
{
	static PanelState empty;
	if (_currentPanel == CFileCommanderPlugin::PluginUnknownPanel)
		return empty;

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return empty;
	else
		return state->second;
}

const CFileCommanderPlugin::PanelState& CFileCommanderPlugin::currentPanelState() const
{
	static PanelState empty;
	if (_currentPanel == CFileCommanderPlugin::PluginUnknownPanel)
		return empty;

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return empty;
	else
		return state->second;
}

QString CFileCommanderPlugin::currentFolderPath() const
{
	if (_currentPanel == CFileCommanderPlugin::PluginUnknownPanel)
		return QString();

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return QString();
	else
		return state->second.currentFolder;
}

QString CFileCommanderPlugin::currentItemPath() const
{
	const PanelState& panelState = currentPanelState();
	if (panelState.currentItemHash != 0)
	{
		auto fileSystemObject = panelState.panelContents.find(panelState.currentItemHash);
		assert(fileSystemObject != panelState.panelContents.end());
		return fileSystemObject->second.absoluteFilePath();
	}
	else 
		return QString();
}

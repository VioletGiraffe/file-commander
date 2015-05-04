#include "cpluginproxy.h"
#include <assert.h>

CPluginProxy::CPluginProxy() :
	_currentPanel(PluginUnknownPanel)
{
}

void CPluginProxy::setToolMenuEntryCreatorImplementation(CPluginProxy::CreateToolMenuEntryImplementationType implementation)
{
	_createToolMenuEntryImplementation = implementation;
}

void CPluginProxy::createToolMenuEntries(std::vector<MenuTree> menuEntries)
{
	if (_createToolMenuEntryImplementation)
		_createToolMenuEntryImplementation(menuEntries);
}

void CPluginProxy::panelContentsChanged(PanelPosition panel, const QString &folder, std::map<qulonglong, CFileSystemObject>& contents)
{
	PanelState& state = _panelState[panel];

	state.panelContents = contents;
	state.currentFolder = folder;
}

void CPluginProxy::selectionChanged(PanelPosition panel, std::vector<qulonglong> selectedItemsHashes)
{
	PanelState& state = _panelState[panel];
	state.selectedItemsHashes = selectedItemsHashes;
}

void CPluginProxy::currentItemChanged(PanelPosition panel, qulonglong currentItemHash)
{
	PanelState& state = _panelState[panel];
	state.currentItemHash = currentItemHash;
}

void CPluginProxy::currentPanelChanged(PanelPosition panel)
{
	_currentPanel = panel;
}


PanelState& CPluginProxy::currentPanelState()
{
	static PanelState empty;
	if (_currentPanel == PluginUnknownPanel)
	{
		empty = PanelState();
		return empty;
	}

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
	{
		empty = PanelState();
		return empty;
	}
	else
		return state->second;
}

const PanelState& CPluginProxy::currentPanelState() const
{
	static const PanelState empty;
	if (_currentPanel == PluginUnknownPanel)
		return empty;

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return empty;
	else
		return state->second;
}

QString CPluginProxy::currentFolderPath() const
{
	if (_currentPanel == PluginUnknownPanel)
		return QString();

	const auto state = _panelState.find(_currentPanel);
	if (state == _panelState.end())
		return QString();
	else
		return state->second.currentFolder;
}

QString CPluginProxy::currentItemPath() const
{
	return currentItem().fullAbsolutePath();
}

const CFileSystemObject &CPluginProxy::currentItem() const
{
	const PanelState& panelState = currentPanelState();
	if (panelState.currentItemHash != 0)
	{
		auto fileSystemObject = panelState.panelContents.find(panelState.currentItemHash);
		assert(fileSystemObject != panelState.panelContents.end());
		return fileSystemObject->second;
	}
	else
	{
		static const CFileSystemObject dummy;
		return dummy;
	}
}

bool CPluginProxy::currentItemIsFile() const
{
	return currentItem().isFile();
}

bool CPluginProxy::currentItemIsDir() const
{
	return currentItem().isDir();
}

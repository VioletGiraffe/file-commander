#include "cpluginproxy.h"
#include "assert/advanced_assert.h"

void CPluginProxy::setToolMenuEntryCreatorImplementation(const CreateToolMenuEntryImplementationType& implementation)
{
	_createToolMenuEntryImplementation = implementation;
}

void CPluginProxy::createToolMenuEntries(const std::vector<MenuTree>& menuTrees)
{
	if (_createToolMenuEntryImplementation)
		_createToolMenuEntryImplementation(menuTrees);
}

void CPluginProxy::createToolMenuEntries(const MenuTree& menuTree)
{
	createToolMenuEntries(std::vector<MenuTree>(1, menuTree));
}

void CPluginProxy::panelContentsChanged(PanelPosition panel, const QString &folder, const std::map<qulonglong, CFileSystemObject>& contents)
{
	PanelState& state = _panelState[panel];

	state.panelContents = contents;
	state.currentFolder = folder;
}

void CPluginProxy::selectionChanged(PanelPosition panel, const std::vector<qulonglong/*hash*/>& selectedItemsHashes)
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


PanelPosition CPluginProxy::currentPanel() const
{
	return _currentPanel;
}

PanelState& CPluginProxy::panelState(const PanelPosition panel)
{
	static PanelState empty;
	if (panel == PluginUnknownPanel)
	{
		assert_unconditional_r("Unknown panel");
		empty = PanelState();
		return empty;
	}

	const auto state = _panelState.find(panel);
	if (state == _panelState.end())
	{
		assert_unconditional_r("Unknown panel");
		empty = PanelState();
		return empty;
	}
	else
		return state->second;
}

const PanelState & CPluginProxy::panelState(const PanelPosition panel) const
{
	static const PanelState empty;
	if (panel == PluginUnknownPanel)
	{
		return empty;
	}

	const auto state = _panelState.find(panel);
	assert_and_return_r(state != _panelState.end(), empty);

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
	static const CFileSystemObject dummy;

	const PanelState& state = panelState(currentPanel());
	if (state.currentItemHash != 0)
	{
		auto fileSystemObject = state.panelContents.find(state.currentItemHash);
		assert_and_return_r(fileSystemObject != state.panelContents.end(), dummy);
		return fileSystemObject->second;
	}
	else
		return dummy;
}


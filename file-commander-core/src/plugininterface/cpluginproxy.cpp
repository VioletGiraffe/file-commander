#include "cpluginproxy.h"
#include "assert/advanced_assert.h"

#include <utility>

CPluginProxy::CPluginProxy(std::function<void(std::function<void()>)> execOnUiThreadImplementation) :
	_execOnUiThreadImplementation(std::move(execOnUiThreadImplementation))
{
	assert_r(_execOnUiThreadImplementation);
}

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

PanelPosition CPluginProxy::otherPanel() const
{
	assert_and_return_r(_currentPanel != PluginUnknownPanel, PluginUnknownPanel);

	return _currentPanel == PluginLeftPanel ? PluginRightPanel : PluginLeftPanel;
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
	static const PanelState empty {};
	if (panel == PluginUnknownPanel)
	{
		return empty;
	}

	const auto state = _panelState.find(panel);
	assert_and_return_r(state != _panelState.end(), empty);

	return state->second;
}

QString CPluginProxy::currentFolderPathForPanel(const PanelPosition panel) const
{
	assert_and_return_r(panel != PluginUnknownPanel, QString());

	const auto state = _panelState.find(panel);
	assert_and_return_r(state != _panelState.end(), QString());

	return state->second.currentFolder;
}

QString CPluginProxy::currentItemPathForPanel(const PanelPosition panel) const
{
	return currentItemForPanel(panel).fullAbsolutePath();
}

const CFileSystemObject& CPluginProxy::currentItemForPanel(const PanelPosition panel) const
{
	static const CFileSystemObject dummy;

	const PanelState& state = panelState(panel);
	if (state.currentItemHash != 0)
	{
		auto fileSystemObject = state.panelContents.find(state.currentItemHash);
		assert_and_return_r(fileSystemObject != state.panelContents.end(), dummy);

		return fileSystemObject->second;
	}
	else
		return dummy;
}

const CFileSystemObject& CPluginProxy::currentItem() const
{
	return currentItemForPanel(currentPanel());
}

QString CPluginProxy::currentItemPath() const
{
	return currentItemPathForPanel(currentPanel());
}

void CPluginProxy::execOnUiThread(const std::function<void()>& code)
{
	_execOnUiThreadImplementation(code);
}

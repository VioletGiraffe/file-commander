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

void CPluginProxy::panelContentsChanged(PanelPosition panel, const QString &folder, const FileListHashMap& contents)
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
	if (panel == PluginUnknownPanel) [[unlikely]]
	{
		assert_unconditional_r("Unknown panel");
		empty = PanelState();
		return empty;
	}

	return _panelState[panel];
}

const PanelState & CPluginProxy::panelState(const PanelPosition panel) const
{
	static const PanelState empty;
	if (panel == PluginUnknownPanel) [[unlikely]]
		return empty;

	return _panelState[panel];
}

QString CPluginProxy::currentFolderPathForPanel(const PanelPosition panel) const
{
	assert_and_return_r(panel != PluginUnknownPanel, QString());

	return _panelState[panel].currentFolder;
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

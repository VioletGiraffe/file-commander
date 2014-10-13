#include "cfilecommanderplugin.h"
#include "cpluginproxy.h"

#include <assert.h>

CFileCommanderPlugin::CFileCommanderPlugin() :
	_currentPanel(PluginUnknownPanel),
	_proxy(nullptr)
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

	for (auto hash: selectedItemsHashes)
	{
		qDebug() << state.panelContents[hash].fullName();
	}
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

void CFileCommanderPlugin::setProxy(CPluginProxy *proxy)
{
	assert(proxy);
	_proxy = proxy;
	proxySet();
}

CFileCommanderPlugin::PanelState& CFileCommanderPlugin::currentPanelState()
{
	static PanelState empty;
	if (_currentPanel == CFileCommanderPlugin::PluginUnknownPanel)
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

const CFileCommanderPlugin::PanelState& CFileCommanderPlugin::currentPanelState() const
{
	static const PanelState empty;
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
	return currentItem().absoluteFilePath();
}

const CFileSystemObject &CFileCommanderPlugin::currentItem() const
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

bool CFileCommanderPlugin::currentItemIsFile() const
{
	return currentItem().isFile();
}

bool CFileCommanderPlugin::currentItemIsDir() const
{
	return currentItem().isDir();
}

void CFileCommanderPlugin::proxySet()
{

}

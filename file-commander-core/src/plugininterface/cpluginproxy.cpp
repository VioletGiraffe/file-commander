#include "cpluginproxy.h"
#include "ccontroller.h"

#include <utility>

// The proxy is the boundary between the plugin and core panel vocabularies, so the translation lives here.
namespace {
[[nodiscard]] Panel corePanel(PanelPosition p) noexcept
{
	switch (p)
	{
	case PluginLeftPanel:
		return Panel::LeftPanel;
	case PluginRightPanel:
		return Panel::RightPanel;
	default:
		return Panel::UnknownPanel;
	}
}

[[nodiscard]] PanelPosition pluginPanel(Panel p) noexcept
{
	switch (p)
	{
	case Panel::LeftPanel:
		return PluginLeftPanel;
	case Panel::RightPanel:
		return PluginRightPanel;
	default:
		return PluginUnknownPanel;
	}
}
}

CPluginProxy::CPluginProxy(CController& controller) noexcept : _controller(controller)
{
}

CPluginProxy::~CPluginProxy()
{
	unsubscribeFromPanelContents();
	_controller.threadPool().retire(taskTag());
}

void CPluginProxy::subscribeToPanelContents(PanelContentsListener listener)
{
	_controller.subscribeToPanelContents(this, [listener = std::move(listener)](Panel p, const QString& folder, const FileListHashMap& contents) {
		listener(pluginPanel(p), folder, contents);
	});
}

void CPluginProxy::unsubscribeFromPanelContents()
{
	_controller.unsubscribeFromPanelContents(this);
}

void CPluginProxy::createToolMenuEntries(const std::vector<MenuTree>& menuTrees)
{
	_controller.createToolMenuEntries(menuTrees);
}

void CPluginProxy::createToolMenuEntries(const MenuTree& menuTree)
{
	createToolMenuEntries(std::vector<MenuTree>(1, menuTree));
}

PanelPosition CPluginProxy::currentPanel() const
{
	if (_controller.hasShutDown())
		return PluginUnknownPanel;

	return pluginPanel(_controller.activePanelPosition());
}

PanelPosition CPluginProxy::otherPanel() const
{
	switch (currentPanel())
	{
	case PluginLeftPanel:
		return PluginRightPanel;
	case PluginRightPanel:
		return PluginLeftPanel;
	default:
		return PluginUnknownPanel;
	}
}

QString CPluginProxy::currentFolderPathForPanel(const PanelPosition panel) const
{
	const Panel p = corePanel(panel);
	if (_controller.hasShutDown() || p == Panel::UnknownPanel)
		return {};

	return _controller.panel(p).currentDirPathPosix();
}

QString CPluginProxy::currentItemPathForPanel(const PanelPosition panel) const
{
	return currentItemForPanel(panel).fullAbsolutePath();
}

CFileSystemObject CPluginProxy::currentItemForPanel(const PanelPosition panel) const
{
	const Panel p = corePanel(panel);
	if (_controller.hasShutDown() || p == Panel::UnknownPanel)
		return {};

	const CPanel& activeTab = _controller.panel(p);
	return activeTab.itemByHash(activeTab.currentItemHashForFolder(activeTab.currentDirPathPosix()));
}

std::vector<qulonglong> CPluginProxy::selectedItemsForPanel(const PanelPosition panel) const
{
	const Panel p = corePanel(panel);
	if (_controller.hasShutDown() || p == Panel::UnknownPanel)
		return {};

	return _controller.selectedItemsHashes(p);
}

CFileSystemObject CPluginProxy::currentItem() const
{
	return currentItemForPanel(currentPanel());
}

QString CPluginProxy::currentItemPath() const
{
	return currentItemPathForPanel(currentPanel());
}

void CPluginProxy::execOnUiThread(const std::function<void()>& code)
{
	if (_controller.hasShutDown())
		return; // Nothing drains the UI queue any more

	_controller.execOnUiThread(code);
}

void CPluginProxy::enqueue(std::function<void()> task)
{
	_controller.threadPool().enqueue(std::move(task), taskTag());
}

void CPluginProxy::parallelFor(size_t count, const std::function<void(size_t)>& fn)
{
	_controller.threadPool().parallelFor(count, fn);
}

uint64_t CPluginProxy::taskTag() const noexcept
{
	// Reuse-safe: ~CPluginProxy retires this tag before the address can be recycled by another proxy.
	return reinterpret_cast<uint64_t>(this);
}

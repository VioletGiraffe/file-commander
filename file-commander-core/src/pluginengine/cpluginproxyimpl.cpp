#include "cpluginproxyimpl.h"

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

CPluginProxyImpl::CPluginProxyImpl(CController& controller) noexcept : _controller(controller)
{
}

CPluginProxyImpl::~CPluginProxyImpl()
{
	unsubscribeFromPanelContents();
	_controller.threadPool().retire(taskTag());
}

void CPluginProxyImpl::subscribeToPanelContents(PanelContentsListener listener)
{
	_controller.subscribeToPanelContents(this, [listener = std::move(listener)](Panel p, const QString& folder, const FileListHashMap& contents) {
		listener(pluginPanel(p), folder, contents);
	});
}

void CPluginProxyImpl::unsubscribeFromPanelContents()
{
	_controller.unsubscribeFromPanelContents(this);
}

void CPluginProxyImpl::createToolMenuEntries(const std::vector<MenuTree>& menuTrees)
{
	for (const auto& menuTree : menuTrees)
		_toolMenuEntries.push_back(menuTree);
}

void CPluginProxyImpl::createToolMenuEntries(const MenuTree& menuTree)
{
	_toolMenuEntries.push_back(menuTree);
}

const std::vector<CPluginProxy::MenuTree>& CPluginProxyImpl::toolMenuEntries() const noexcept
{
	return _toolMenuEntries;
}

PanelPosition CPluginProxyImpl::currentPanel() const
{
	return pluginPanel(_controller.activePanelPosition());
}

PanelPosition CPluginProxyImpl::otherPanel() const
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

QString CPluginProxyImpl::currentFolderPathForPanel(const PanelPosition panel) const
{
	return _controller.currentFolderPath(corePanel(panel));
}

QString CPluginProxyImpl::currentItemPathForPanel(const PanelPosition panel) const
{
	return currentItemForPanel(panel).fullAbsolutePath();
}

CFileSystemObject CPluginProxyImpl::currentItemForPanel(const PanelPosition panel) const
{
	return _controller.currentItem(corePanel(panel));
}

std::vector<qulonglong> CPluginProxyImpl::selectedItemsForPanel(const PanelPosition panel) const
{
	return _controller.selectedItemsHashes(corePanel(panel));
}

CFileSystemObject CPluginProxyImpl::currentItem() const
{
	return _controller.currentItem();
}

QString CPluginProxyImpl::currentItemPath() const
{
	return currentItem().fullAbsolutePath();
}

void CPluginProxyImpl::execOnUiThread(const std::function<void()>& code)
{
	_controller.execOnUiThread(code);
}

void CPluginProxyImpl::enqueue(std::function<void()> task)
{
	_controller.threadPool().enqueue(std::move(task), taskTag());
}

void CPluginProxyImpl::parallelFor(size_t count, const std::function<void(size_t)>& fn)
{
	_controller.threadPool().parallelFor(count, fn);
}

uint64_t CPluginProxyImpl::taskTag() const noexcept
{
	// Reuse-safe: ~CPluginProxyImpl retires this tag before the address can be recycled by another proxy.
	return reinterpret_cast<uint64_t>(this);
}

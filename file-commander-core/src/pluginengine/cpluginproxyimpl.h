#pragma once

#include "plugininterface/cpluginproxy.h"

#include <cstdint>
#include <vector>

class CController;

// The application's implementation of CPluginProxy.
// The instance address identifies the plugin to controller-owned resources, and ~CPluginProxyImpl releases them.
class CPluginProxyImpl final : public CPluginProxy
{
public:
	explicit CPluginProxyImpl(CController& controller) noexcept;
	~CPluginProxyImpl() override;

	void createToolMenuEntries(const std::vector<MenuTree>& menuTrees) override;
	void createToolMenuEntries(const MenuTree& menuTree) override;
	[[nodiscard]] const std::vector<MenuTree>& toolMenuEntries() const noexcept;

	void subscribeToPanelContents(PanelContentsListener listener) override;
	void unsubscribeFromPanelContents() override;

	[[nodiscard]] PanelPosition currentPanel() const override;
	[[nodiscard]] PanelPosition otherPanel() const override;
	[[nodiscard]] QString currentFolderPathForPanel(PanelPosition panel) const override;
	[[nodiscard]] QString currentItemPathForPanel(PanelPosition panel) const override;
	[[nodiscard]] CFileSystemObject currentItemForPanel(PanelPosition panel) const override;
	[[nodiscard]] std::vector<qulonglong> selectedItemsForPanel(PanelPosition panel) const override;

	[[nodiscard]] CFileSystemObject currentItem() const override;
	[[nodiscard]] QString currentItemPath() const override;

	void execOnUiThread(const std::function<void()>& code) override;
	void enqueue(std::function<void()> task) override;
	void parallelFor(size_t count, const std::function<void(size_t)>& fn) override;

private:
	[[nodiscard]] uint64_t taskTag() const noexcept;

	CController& _controller;
	std::vector<MenuTree> _toolMenuEntries;
};

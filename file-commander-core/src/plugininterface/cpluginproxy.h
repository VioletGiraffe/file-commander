#pragma once
#include "cfilesystemobject.h"
#include "detail/file_list_hashmap.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

class CController;

enum PanelPosition : int {PluginLeftPanel, PluginRightPanel, PluginUnknownPanel};

// A plugin's only interface to the application. One instance per plugin, created and owned by CPluginEngine:
// the instance address identifies the plugin to controller-owned resources, and ~CPluginProxy releases them.
// Queries answer nothing once the controller has shut down, since the panels are gone by then.
class CPluginProxy
{
public:
	struct MenuTree {
		inline MenuTree(QString name_, std::function<void()>&& handler_, QIcon icon_ = {}):
			name(std::move(name_)), icon(std::move(icon_)), handler(std::move(handler_))
		{}

		const QString name;
		const QIcon icon;
		const std::function<void()> handler;
		std::vector<MenuTree> children;
	};

	using CreateToolMenuEntryImplementationType = std::function<void(const std::vector<CPluginProxy::MenuTree>&)>;

	explicit CPluginProxy(CController& controller) noexcept;
	~CPluginProxy();

	CPluginProxy(const CPluginProxy&) = delete;
	CPluginProxy& operator=(const CPluginProxy&) = delete;

// UI access for plugins; every plugin is only supposed to call this method once. Main thread only.
	void createToolMenuEntries(const std::vector<MenuTree>& menuTrees);
	void createToolMenuEntries(const MenuTree& menuTree);

// Contents of the panel the user is looking at
	// Fires for each side that already has a listing when you subscribe, and on every change afterwards, so a
	// subscription to an idle panel still receives its contents. Never fires between a navigation and its listing,
	// so folder and contents always agree. Main thread only; the contents are only valid for the duration of the
	// call, and the panel cannot refresh until it returns - copy what you need and get out. Subscription changes
	// made by a listener apply after the current delivery batch.
	using PanelContentsListener = std::function<void(PanelPosition panel, const QString& folder, const FileListHashMap& contents)>;
	void subscribeToPanelContents(PanelContentsListener listener);
	void unsubscribeFromPanelContents();

// Visible panel state
	[[nodiscard]] PanelPosition currentPanel() const;
	[[nodiscard]] PanelPosition otherPanel() const;
	[[nodiscard]] QString currentFolderPathForPanel(PanelPosition panel) const;
	[[nodiscard]] QString currentItemPathForPanel(PanelPosition panel) const;
	[[nodiscard]] CFileSystemObject currentItemForPanel(PanelPosition panel) const;
	[[nodiscard]] std::vector<qulonglong> selectedItemsForPanel(PanelPosition panel) const;

	[[nodiscard]] CFileSystemObject currentItem() const;
	[[nodiscard]] QString currentItemPath() const;

// Threading
	void execOnUiThread(const std::function<void()>& code);
	// Background work on the application's shared pool. Every task carries this proxy's tag and ~CPluginProxy
	// retires it, so no task outlives the plugin that queued it.
	void enqueue(std::function<void()> task);
	// Returns only once every index has been processed, which is why it needs no tag of its own.
	void parallelFor(size_t count, const std::function<void(size_t)>& fn);

private:
	[[nodiscard]] uint64_t taskTag() const noexcept;

	CController& _controller;
};

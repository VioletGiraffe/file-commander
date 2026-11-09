#pragma once

#include "cfilesystemobject.h"
#include "detail/file_list_hashmap.h"


DISABLE_COMPILER_WARNINGS
#include <QIcon>
RESTORE_COMPILER_WARNINGS

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

enum PanelPosition : int {PluginLeftPanel, PluginRightPanel, PluginUnknownPanel};

// A plugin's only interface to the application. One instance per plugin, owned by CPluginEngine; CPluginProxyImpl implements it.
// Must stay purely virtual: a plugin links its own copy of the core, so a non-virtual call would run that copy on the application's objects.
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

	CPluginProxy(const CPluginProxy&) = delete;
	CPluginProxy& operator=(const CPluginProxy&) = delete;

// Commands menu entries. Only calls from proxySet() take effect: the app builds the menu once all plugins have loaded.
	virtual void createToolMenuEntries(const std::vector<MenuTree>& menuTrees) = 0;
	virtual void createToolMenuEntries(const MenuTree& menuTree) = 0;

// Contents of the panel the user is looking at
	// Fires for each side that already has a listing when you subscribe, and on every change afterwards, so a
	// subscription to an idle panel still receives its contents. Never fires between a navigation and its listing,
	// so folder and contents always agree. Main thread only; the contents are only valid for the duration of the
	// call, and the panel cannot refresh until it returns - copy what you need and get out. Subscription changes
	// made by a listener apply after the current delivery batch.
	using PanelContentsListener = std::function<void(PanelPosition panel, const QString& folder, const FileListHashMap& contents)>;
	virtual void subscribeToPanelContents(PanelContentsListener listener) = 0;
	virtual void unsubscribeFromPanelContents() = 0;

// Visible panel state
	[[nodiscard]] virtual PanelPosition currentPanel() const = 0;
	[[nodiscard]] virtual PanelPosition otherPanel() const = 0;
	[[nodiscard]] virtual QString currentFolderPathForPanel(PanelPosition panel) const = 0;
	[[nodiscard]] virtual QString currentItemPathForPanel(PanelPosition panel) const = 0;
	[[nodiscard]] virtual CFileSystemObject currentItemForPanel(PanelPosition panel) const = 0;
	// Empty when nothing is selected: the item under the cursor comes from currentItemForPanel()
	[[nodiscard]] virtual std::vector<qulonglong> selectedItemsForPanel(PanelPosition panel) const = 0;

	[[nodiscard]] virtual CFileSystemObject currentItem() const = 0;
	[[nodiscard]] virtual QString currentItemPath() const = 0;

// Threading
	virtual void execOnUiThread(const std::function<void()>& code) = 0;
	// Background work on the application's shared pool. Every task carries this proxy's tag, and destroying the
	// proxy retires the tag, so no task outlives the plugin that queued it.
	virtual void enqueue(std::function<void()> task) = 0;
	// Returns only once every index has been processed, which is why it needs no tag of its own.
	virtual void parallelFor(size_t count, const std::function<void(size_t)>& fn) = 0;

protected:
	CPluginProxy() noexcept = default;
	virtual ~CPluginProxy() = default;
};

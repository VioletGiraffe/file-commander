# Plugin system

There are two unrelated mechanisms:

1. Native File Commander plugins implement `CFileCommanderPlugin` and are loaded by `CPluginEngine`.
2. Windows WCX archive modules are discovered by `CWcxPluginHost`; this is currently only a loader, not panel
   archive integration.

## Native plugin source map

| Concern | Source |
|---------|--------|
| ABI and base interfaces | `file-commander-core/src/plugininterface/cfilecommanderplugin.*`, `cfilecommanderviewerplugin.*`, `cfilecommandertoolplugin.*` |
| Plugin-to-application facade | `file-commander-core/src/plugininterface/cpluginproxy.*` |
| Window ownership | `file-commander-core/src/plugininterface/cpluginwindow.*` |
| Discovery and viewer selection | `file-commander-core/src/pluginengine/cpluginengine.*` |
| Quick-view host | `qt-app/src/panel/cpaneldisplaycontroller.*` |
| Commands-menu materialization | `qt-app/src/cmainwindow.*` |

Each native library exports `createPlugin()`. The engine then supplies the proxy; `proxySet()` is the hook for
proxy-dependent initialization. The interface headers are authoritative for the ABI.

`main()` owns the plugin engine between the main window and controller in the destruction order. The engine owns
each plugin's instance, proxy, and library module. Teardown destroys the proxy first so its tagged background work
is retired while the plugin instance and module are still alive, then destroys the instance and unloads the module.
Plugin destructors must not use their proxy.

Plugin windows use `CFileCommanderViewerPlugin::WindowPtr`, a unique pointer whose deleter is instantiated in the
plugin module. Keep that type across the boundary so allocation and deletion occur in the same dynamic library.
Full viewer windows opt into deletion on close; quick view retains the `WindowPtr` in `CPanelDisplayController`.
A window embedded as quick view is never shown, so its menus and shortcuts are unreachable; `configureForQuickView()`
is the hook for adapting to that.

## Proxy and tab visibility

`CPluginProxy` exposes committed active-tab contents subscriptions, current/other-panel queries, UI-thread dispatch,
tagged work on the shared application pool, and menu registration. Plugins do not receive `CController` or
`CPluginEngine` directly.

The controller publishes only a side's active tab and never publishes `onPanelContentsInvalidated`; subscribers
therefore receive a matching folder/list pair only after contents commit. Selection, current-item, and focus state
come from the visible UI triplets. A controller-owned access gate serializes shutdown with proxy queries and UI
dispatch; once their backing state has been removed, those methods return empty or no-op internally.

Panel-content delivery does not copy the file-list map. A subscriber borrows the committed `FileListHashMap` by
reference while that panel's list mutex is held; the reference is valid only for the callback, and panel publication
cannot proceed until it returns. Copy only the data that must outlive the callback and keep the callback short.

## Discovery and viewer selection

`CPluginEngine::loadPlugins()` discovers platform libraries beside the application, skips symlinks, resolves the
factory, and retains both plugin and library objects. Consult the implementation for filename patterns.

Automatic viewer selection asks each viewer's `canViewFile()`. The text viewer accepts essentially any file, so its
well-known `ViewerCategory::Text` is held as a fallback until specialized viewers have declined. Shift+F3 restricts
selection to that category. Preserve this ordering; plugin discovery order is not a priority mechanism.

## WCX status

On Windows, `CWcxPluginHost` loads `*.wcx64` libraries and resolves a limited symbol subset. The non-Windows host is
a stub. No code mounts WCX contents as a `CPanel` filesystem, so the loader does not imply archive browsing.

Plugin `.pro` files are authoritative for sources, dependencies, output names, and platform branches.

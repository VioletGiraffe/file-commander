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
plugin instances and their library modules; each instance is destroyed before its module is unloaded. The
controller owns the proxy, so it remains valid through plugin destruction.

Plugin windows use `CFileCommanderViewerPlugin::WindowPtr`, a unique pointer whose deleter is instantiated in the
plugin module. Keep that type across the boundary so allocation and deletion occur in the same dynamic library.
Full viewer windows opt into deletion on close; quick view retains the `WindowPtr` in `CPanelDisplayController`.

## Proxy and tab visibility

`CPluginProxy` exposes visible-panel snapshots, current/other-panel queries, UI-thread dispatch, and menu
registration. Plugins do not receive `CController` or `CPluginEngine` directly.

The controller publishes only a side's active tab into the proxy. It ignores `onPanelContentsInvalidated`, retaining
the last consistent folder/list pair until committed contents arrive; an intermediate update would otherwise pair
a new folder path with an empty list. Selection/current-item/focus updates come from the UI for the visible
triplets. Controller shutdown disables proxy callbacks and destroys queued plugin callables while their modules are
still loaded.

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

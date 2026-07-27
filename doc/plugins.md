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

Each native library exports `extern "C" CFileCommanderPlugin* createPlugin()`. After creation, the engine calls
`setProxy()`; `proxySet()` is the initialization hook for registering menus or other proxy-dependent setup. Read the
interface headers for the current virtual API rather than copying it here.

Plugin windows use `CFileCommanderViewerPlugin::WindowPtr`, a unique pointer whose deleter is instantiated in the
plugin module. Keep that type across the boundary so allocation and deletion occur in the same dynamic library.
Full viewer windows opt into deletion on close; quick view retains the `WindowPtr` in `CPanelDisplayController`.

## Proxy and tab visibility

`CPluginProxy` exposes snapshots of both visible panels: current folder, file-list map, selection, and current item.
It also provides current/other-panel queries, UI-thread dispatch, and recursive `MenuTree` registration. Plugins do
not receive `CController` directly.

The plugin engine is attached to every tab but publishes only a side's active tab. It ignores
`onPanelContentsInvalidated`, retaining the last consistent folder/list pair until committed contents arrive; an
intermediate update would otherwise pair a new folder path with an empty list. Selection/current-item/focus updates
come from the UI for the visible triplets.

## Discovery and viewer selection

`CPluginEngine::loadPlugins()` scans the application directory for the platform-specific `*plugin_*` library
pattern, skips symlinks, resolves `createPlugin`, and retains the plugin and `QLibrary` objects. Consult
`cpluginengine.cpp` for the exact filename suffixes.

Automatic viewer selection asks each viewer's `canViewFile()`. The text viewer accepts essentially any file, so its
well-known `ViewerCategory::Text` is held as a fallback until specialized viewers have declined. Shift+F3 restricts
selection to that category. Preserve this ordering; plugin discovery order is not a priority mechanism.

## WCX status

On Windows, `CWcxPluginHost` scans the application directory for `*.wcx64`, loads each library, and currently checks
only the small symbol subset in `cwcxpluginhost.cpp`. The non-Windows host is a stub. No code mounts WCX contents as a
`CPanel` filesystem, so do not infer archive browsing from the presence of the loader.

## Shipped native plugins

| Plugin | Source | Notes |
|--------|--------|-------|
| Image viewer | `plugins/viewer/imageviewer/` | Viewer backed by the `image-processing` submodule. |
| Text viewer | `plugins/viewer/textviewer/` | Viewer using `text-encoding-detector` and vendored qutepart syntax highlighting. |
| File comparison | `plugins/tools/filecomparisonplugin/` | Tool-menu command using core `CFileComparator`; its header/member order and shutdown callback document worker/dialog lifetime. |

The plugin `.pro` files are authoritative for sources, dependencies, output names, and platform branches. The
top-level `file-commander.pro` records their build ordering relative to core and the application.

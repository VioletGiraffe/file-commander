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
| Viewer widget contract | `file-commander-core/src/plugininterface/cfileviewer.*` |
| Discovery and viewer selection | `file-commander-core/src/pluginengine/cpluginengine.*` |
| Viewer frames: quick view, F3 window | `qt-app/src/panel/cpaneldisplaycontroller.*`, `qt-app/src/viewerwindow/` |
| Commands-menu materialization | `qt-app/src/cmainwindow.*` |

Each native library exports `pluginInterfaceVersion()` and `createPlugin()`. The engine resolves and checks the
version before calling the factory, and skips the library on mismatch or absence: the returned object's virtuals are
dispatched through the caller's vtable layout, so a stale plugin is undefined behaviour rather than a clean failure.
Bump `PLUGIN_INTERFACE_VERSION` in `cfilecommanderplugin.h` whenever these interfaces change. The engine then
supplies the proxy; `proxySet()` is the hook for proxy-dependent initialization. The interface headers are
authoritative for the ABI.

`main()` owns the plugin engine between the main window and controller in the destruction order. The engine owns
each plugin's instance, proxy, and library module. Teardown destroys the proxy first so its tagged background work
is retired while the plugin instance and module are still alive, then destroys the instance. Modules stay mapped
until the process exits. Plugin destructors must not use their proxy.

## Viewers

A viewer plugin creates `CFileViewer` widgets; the app owns them through Qt parenting and frames them itself, in a
window (F3) or in a file panel (quick view). The widget never learns which, so:

- `loadFile()` is repeatable: quick view loads file after file into the same widget, keeping the user's view settings.
  A failed load shows the error inside the widget; no viewer puts up a dialog for it.
- Menus come from `addMenu()`, which also scopes every shortcut to the widget. The hosting window shows them in its
  menu bar; the base class offers them as the context menu in both frames.
- Dialogs are parented to the widget, and window decoration goes on it too: the F3 frame follows `windowIconChanged`.
- `closeFile()` precedes deleting, moving or renaming the viewed file or a folder holding it; a viewer that keeps its
  file open releases it there.

## Proxy and tab visibility

`CPluginProxy` exposes committed active-tab contents subscriptions, current/other-panel queries, UI-thread dispatch,
tagged work on the shared application pool, and menu registration. Plugins do not receive `CController` or
`CPluginEngine` directly.

The controller publishes only a side's active tab and never publishes `onPanelContentsInvalidated`; subscribers
therefore receive a matching folder/list pair only after contents commit. Selection, current-item, and focus state
come from the tabs on screen. A controller-owned access gate serializes shutdown with proxy queries and UI
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
